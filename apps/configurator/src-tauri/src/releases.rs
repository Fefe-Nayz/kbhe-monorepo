use crate::signing::{verify_app_asset, verify_firmware_asset};
use crate::updater_compat::{inspect_firmware_artifact, FirmwareArtifact};
use reqwest::blocking::Client;
use semver::Version;
use serde::{Deserialize, Serialize};
use std::fs::{File, OpenOptions};
use std::io::{copy, Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;

const APP_TAG_PREFIX: &str = "app-v";
const FIRMWARE_TAG_PREFIX: &str = "firmware-v";
const MAX_APP_INSTALLER_BYTES: u64 = 512 * 1024 * 1024;
const MAX_FIRMWARE_BYTES: u64 = 0x0002_FF00;
const MAX_MIGRATION_PACKAGE_BYTES: u64 = 0x0002_FF54;
const ED25519_SIGNATURE_BYTES: u64 = 64;
static APP_INSTALLER_LAUNCHED: AtomicBool = AtomicBool::new(false);

struct AppInstallerLaunchGuard {
    reset_on_drop: bool,
}

impl AppInstallerLaunchGuard {
    fn acquire() -> Result<Self, String> {
        APP_INSTALLER_LAUNCHED
            .compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire)
            .map_err(|_| {
                "an app installer has already been launched; restart the configurator to retry"
                    .to_string()
            })?;
        Ok(Self {
            reset_on_drop: true,
        })
    }

    fn mark_launched(&mut self) {
        self.reset_on_drop = false;
    }
}

impl Drop for AppInstallerLaunchGuard {
    fn drop(&mut self) {
        if self.reset_on_drop {
            APP_INSTALLER_LAUNCHED.store(false, Ordering::Release);
        }
    }
}

#[derive(Debug, Deserialize)]
struct GithubRelease {
    tag_name: String,
    name: Option<String>,
    body: Option<String>,
    html_url: String,
    prerelease: bool,
    draft: bool,
    published_at: Option<String>,
    assets: Vec<GithubAsset>,
}

#[derive(Debug, Deserialize, Clone)]
struct GithubAsset {
    name: String,
    browser_download_url: String,
    size: u64,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ReleaseUpdateInfo {
    update_available: bool,
    version: Option<String>,
    tag: Option<String>,
    name: Option<String>,
    notes: Option<String>,
    published_at: Option<String>,
    html_url: Option<String>,
    asset_name: Option<String>,
    asset_size: Option<u64>,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DownloadedFirmware {
    path: String,
    signature_path: String,
    file_name: String,
    version_tag: String,
    firmware_version: DownloadedFirmwareVersion,
    migration_path: Option<String>,
    migration_signature_path: Option<String>,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
struct DownloadedFirmwareVersion {
    major: u8,
    minor: u8,
    patch: u8,
}

fn client() -> Result<Client, String> {
    Client::builder()
        .user_agent("kbhe-configurator")
        .timeout(Duration::from_secs(30))
        .build()
        .map_err(|error| error.to_string())
}

fn release_owner() -> &'static str {
    option_env!("KBHE_RELEASE_OWNER").unwrap_or("Fefe-Nayz")
}

fn release_repo() -> &'static str {
    option_env!("KBHE_RELEASE_REPO").unwrap_or("kbhe-monorepo")
}

fn releases_url() -> String {
    format!(
        "https://api.github.com/repos/{}/{}/releases?per_page=100",
        release_owner(),
        release_repo()
    )
}

fn fetch_releases() -> Result<Vec<GithubRelease>, String> {
    let response = client()?
        .get(releases_url())
        .send()
        .map_err(|error| format!("failed to query GitHub releases: {error}"))?;

    if !response.status().is_success() {
        return Err(format!(
            "GitHub releases request failed: {}",
            response.status()
        ));
    }

    response
        .json::<Vec<GithubRelease>>()
        .map_err(|error| format!("failed to parse GitHub releases: {error}"))
}

fn parse_prefixed_version(tag: &str, prefix: &str) -> Option<Version> {
    let raw = tag.strip_prefix(prefix)?;
    // APP_TAG_PREFIX/FIRMWARE_TAG_PREFIX already include the single canonical
    // `v`. Accepting more `v` characters creates release-tag aliases that can
    // replay the same signed asset under a misleading GitHub release name.
    if raw.starts_with('v') {
        return None;
    }
    let version = Version::parse(raw).ok()?;
    if !version.pre.is_empty() || !version.build.is_empty() {
        return None;
    }
    if prefix == FIRMWARE_TAG_PREFIX
        && (version.major > u64::from(u8::MAX)
            || version.minor > u64::from(u8::MAX)
            || version.patch > u64::from(u8::MAX))
    {
        return None;
    }
    Some(version)
}

fn newer_than_current(candidate: &Version, current: Option<&str>) -> bool {
    let Some(current) = current else {
        return true;
    };

    Version::parse(current.trim_start_matches('v'))
        .map(|current| candidate > &current)
        .unwrap_or(true)
}

fn installer_asset(assets: &[GithubAsset]) -> Option<GithubAsset> {
    let preferred_ext = if cfg!(target_os = "windows") {
        [".exe", ".msi"].as_slice()
    } else if cfg!(target_os = "macos") {
        [".dmg", ".app.tar.gz"].as_slice()
    } else {
        [".appimage", ".deb", ".rpm"].as_slice()
    };

    for ext in preferred_ext {
        if let Some(asset) = assets
            .iter()
            .find(|asset| asset.name.to_lowercase().ends_with(ext))
        {
            return Some(asset.clone());
        }
    }

    None
}

fn firmware_asset(assets: &[GithubAsset]) -> Option<GithubAsset> {
    assets
        .iter()
        .find(|asset| asset.name == "kbhe-app.bin")
        .cloned()
}

fn migration_asset(assets: &[GithubAsset]) -> Option<GithubAsset> {
    assets
        .iter()
        .find(|asset| asset.name == "kbhe-updater-v2-to-v3.bin")
        .cloned()
}

fn signature_asset(assets: &[GithubAsset], signed_asset: &GithubAsset) -> Option<GithubAsset> {
    let expected_name = format!("{}.sig", signed_asset.name);
    assets
        .iter()
        .find(|asset| asset.name == expected_name && asset.size == ED25519_SIGNATURE_BYTES)
        .cloned()
}

fn latest_release_with_asset(
    prefix: &str,
    current_version: Option<&str>,
    pick_asset: fn(&[GithubAsset]) -> Option<GithubAsset>,
) -> Result<Option<(GithubRelease, Version, GithubAsset)>, String> {
    let mut candidates = Vec::new();

    for release in fetch_releases()? {
        if release.draft || release.prerelease {
            continue;
        }

        let Some(version) = parse_prefixed_version(&release.tag_name, prefix) else {
            continue;
        };

        if !newer_than_current(&version, current_version) {
            continue;
        }

        let Some(asset) = pick_asset(&release.assets) else {
            continue;
        };
        if signature_asset(&release.assets, &asset).is_none() {
            continue;
        }

        candidates.push((release, version, asset));
    }

    candidates.sort_by(|(_, left, _), (_, right, _)| right.cmp(left));
    Ok(candidates.into_iter().next())
}

fn update_info(result: Option<(GithubRelease, Version, GithubAsset)>) -> ReleaseUpdateInfo {
    if let Some((release, version, asset)) = result {
        ReleaseUpdateInfo {
            update_available: true,
            version: Some(version.to_string()),
            tag: Some(release.tag_name),
            name: release.name,
            notes: release.body,
            published_at: release.published_at,
            html_url: Some(release.html_url),
            asset_name: Some(asset.name),
            asset_size: Some(asset.size),
        }
    } else {
        ReleaseUpdateInfo {
            update_available: false,
            version: None,
            tag: None,
            name: None,
            notes: None,
            published_at: None,
            html_url: None,
            asset_name: None,
            asset_size: None,
        }
    }
}

#[tauri::command]
pub async fn kbhe_check_app_update(
    current_version: Option<String>,
) -> Result<ReleaseUpdateInfo, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let current = current_version
            .as_deref()
            .or(Some(env!("CARGO_PKG_VERSION")));
        latest_release_with_asset(APP_TAG_PREFIX, current, installer_asset).map(update_info)
    })
    .await
    .map_err(|error| format!("app update worker failed: {error}"))?
}

#[tauri::command]
pub async fn kbhe_check_firmware_update(
    current_version: Option<String>,
) -> Result<ReleaseUpdateInfo, String> {
    tauri::async_runtime::spawn_blocking(move || {
        latest_release_with_asset(
            FIRMWARE_TAG_PREFIX,
            current_version.as_deref(),
            firmware_asset,
        )
        .map(update_info)
    })
    .await
    .map_err(|error| format!("firmware update worker failed: {error}"))?
}

fn sanitize_filename(name: &str) -> String {
    name.chars()
        .map(|ch| {
            if matches!(ch, '\\' | '/' | ':' | '*' | '?' | '"' | '<' | '>' | '|') {
                '_'
            } else {
                ch
            }
        })
        .collect()
}

fn secure_download_directory(kind: &str) -> Result<PathBuf, String> {
    let parent = std::env::temp_dir().join("kbhe-configurator").join(kind);
    std::fs::create_dir_all(&parent).map_err(|error| error.to_string())?;
    reject_reparse_point(&parent)?;
    for _ in 0..16 {
        let mut nonce = [0u8; 16];
        getrandom::fill(&mut nonce)
            .map_err(|error| format!("secure random generation failed: {error}"))?;
        let suffix = nonce
            .iter()
            .map(|byte| format!("{byte:02x}"))
            .collect::<String>();
        let directory = parent.join(suffix);
        #[cfg(unix)]
        let builder = {
            use std::os::unix::fs::DirBuilderExt;
            let mut builder = std::fs::DirBuilder::new();
            builder.mode(0o700);
            builder
        };
        #[cfg(not(unix))]
        let builder = std::fs::DirBuilder::new();
        match builder.create(&directory) {
            Ok(()) => return Ok(directory),
            Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => continue,
            Err(error) => return Err(error.to_string()),
        }
    }
    Err("failed to allocate a unique secure download directory".to_string())
}

fn reject_reparse_point(path: &Path) -> Result<(), String> {
    let metadata = std::fs::symlink_metadata(path).map_err(|error| error.to_string())?;
    if metadata.file_type().is_symlink() {
        return Err(format!("refusing symbolic link: {}", path.display()));
    }
    #[cfg(windows)]
    {
        use std::os::windows::fs::MetadataExt;
        const FILE_ATTRIBUTE_REPARSE_POINT: u32 = 0x400;
        if metadata.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT != 0 {
            return Err(format!(
                "refusing Windows reparse point: {}",
                path.display()
            ));
        }
    }
    Ok(())
}

#[cfg(windows)]
fn lock_verified_installer(path: &Path) -> Result<File, String> {
    use std::os::windows::fs::OpenOptionsExt;
    const FILE_SHARE_READ: u32 = 0x0000_0001;
    OpenOptions::new()
        .read(true)
        .share_mode(FILE_SHARE_READ)
        .open(path)
        .map_err(|error| error.to_string())
}

#[cfg(not(windows))]
fn lock_verified_installer(path: &Path) -> Result<File, String> {
    OpenOptions::new()
        .read(true)
        .open(path)
        .map_err(|error| error.to_string())
}

fn download_asset(
    asset: &GithubAsset,
    directory: &Path,
    maximum_size: u64,
) -> Result<PathBuf, String> {
    if asset.size == 0 || asset.size > maximum_size {
        return Err(format!(
            "refusing {}: release metadata size {} is outside the allowed range",
            asset.name, asset.size
        ));
    }
    reject_reparse_point(directory)?;
    let destination = directory.join(sanitize_filename(&asset.name));
    let response = client()?
        .get(&asset.browser_download_url)
        .send()
        .map_err(|error| format!("failed to download {}: {error}", asset.name))?;

    if !response.status().is_success() {
        return Err(format!(
            "download failed for {}: {}",
            asset.name,
            response.status()
        ));
    }

    if let Some(content_length) = response.content_length() {
        if content_length != asset.size || content_length > maximum_size {
            return Err(format!(
                "refusing {}: response size {} does not match release metadata {}",
                asset.name, content_length, asset.size
            ));
        }
    }

    let mut file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&destination)
        .map_err(|error| format!("refusing non-unique download target: {error}"))?;
    let copied =
        copy(&mut response.take(maximum_size + 1), &mut file).map_err(|error| error.to_string())?;
    if copied != asset.size || copied > maximum_size {
        drop(file);
        let _ = std::fs::remove_file(&destination);
        return Err(format!(
            "refusing {}: downloaded {} bytes, expected {}",
            asset.name, copied, asset.size
        ));
    }
    file.flush().map_err(|error| error.to_string())?;
    file.sync_all().map_err(|error| error.to_string())?;
    reject_reparse_point(&destination)?;
    Ok(destination)
}

fn release_assets_by_tag(
    tag: &str,
    prefix: &str,
    pick_asset: fn(&[GithubAsset]) -> Option<GithubAsset>,
) -> Result<(GithubAsset, GithubAsset), String> {
    if parse_prefixed_version(tag, prefix).is_none() {
        return Err(format!("unexpected release tag for {prefix}: {tag}"));
    }

    let release = fetch_releases()?
        .into_iter()
        .find(|release| release.tag_name == tag && !release.draft && !release.prerelease)
        .ok_or_else(|| format!("stable release {tag} was not found"))?;
    let asset = pick_asset(&release.assets)
        .ok_or_else(|| format!("no downloadable asset found for release {tag}"))?;
    let signature = signature_asset(&release.assets, &asset)
        .ok_or_else(|| format!("release {tag} has no valid {}.sig asset", asset.name))?;
    Ok((asset, signature))
}

#[tauri::command]
pub async fn kbhe_download_firmware_release(tag: String) -> Result<DownloadedFirmware, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let version = parse_prefixed_version(&tag, FIRMWARE_TAG_PREFIX)
            .ok_or_else(|| format!("invalid firmware release tag: {tag}"))?;
        let version_bytes = [
            u8::try_from(version.major).map_err(|_| "firmware major version exceeds 255")?,
            u8::try_from(version.minor).map_err(|_| "firmware minor version exceeds 255")?,
            u8::try_from(version.patch).map_err(|_| "firmware patch version exceeds 255")?,
        ];
        let release = fetch_releases()?
            .into_iter()
            .find(|release| release.tag_name == tag && !release.draft && !release.prerelease)
            .ok_or_else(|| format!("stable release {tag} was not found"))?;
        let asset = firmware_asset(&release.assets)
            .ok_or_else(|| format!("release {tag} has no kbhe-app.bin asset"))?;
        let firmware_signature_asset = signature_asset(&release.assets, &asset)
            .ok_or_else(|| format!("release {tag} has no valid kbhe-app.bin.sig asset"))?;
        let migration_asset = migration_asset(&release.assets);
        let migration_signature_asset = migration_asset
            .as_ref()
            .map(|asset| {
                signature_asset(&release.assets, asset).ok_or_else(|| {
                    format!(
                        "release {tag} has a migration package but no valid {}.sig asset",
                        asset.name
                    )
                })
            })
            .transpose()?;
        let directory = secure_download_directory("firmware")?;
        let path = download_asset(&asset, &directory, MAX_FIRMWARE_BYTES)?;
        let signature_path = download_asset(
            &firmware_signature_asset,
            &directory,
            ED25519_SIGNATURE_BYTES,
        )?;
        let firmware = std::fs::read(&path)
            .map_err(|error| format!("failed to read downloaded firmware: {error}"))?;
        let signature = std::fs::read(&signature_path)
            .map_err(|error| format!("failed to read firmware signature: {error}"))?;
        if let Err(error) = verify_firmware_asset(&firmware, version_bytes, &signature) {
            let _ = std::fs::remove_file(&path);
            let _ = std::fs::remove_file(&signature_path);
            return Err(format!("downloaded firmware is not authentic: {error}"));
        }
        let (migration_path, migration_signature_path) =
            match (migration_asset, migration_signature_asset) {
                (Some(migration_asset), Some(migration_signature_asset)) => {
                    let migration_path =
                        download_asset(&migration_asset, &directory, MAX_MIGRATION_PACKAGE_BYTES)?;
                    let migration_signature_path = download_asset(
                        &migration_signature_asset,
                        &directory,
                        ED25519_SIGNATURE_BYTES,
                    )?;
                    let migration = std::fs::read(&migration_path).map_err(|error| {
                        format!("failed to read downloaded updater migration: {error}")
                    })?;
                    let migration_signature =
                        std::fs::read(&migration_signature_path).map_err(|error| {
                            format!("failed to read updater migration signature: {error}")
                        })?;
                    match inspect_firmware_artifact(&migration, version_bytes, &migration_signature)
                    {
                        Ok(FirmwareArtifact::V2ToV3Migration(_)) => {}
                        Ok(FirmwareArtifact::Application) => {
                            let _ = std::fs::remove_file(&migration_path);
                            let _ = std::fs::remove_file(&migration_signature_path);
                            return Err(
                                "downloaded updater migration has the normal application layout"
                                    .to_string(),
                            );
                        }
                        Err(error) => {
                            let _ = std::fs::remove_file(&migration_path);
                            let _ = std::fs::remove_file(&migration_signature_path);
                            return Err(format!(
                                "downloaded updater migration is not authentic: {error}"
                            ));
                        }
                    }
                    (
                        Some(migration_path.to_string_lossy().into_owned()),
                        Some(migration_signature_path.to_string_lossy().into_owned()),
                    )
                }
                (None, None) => (None, None),
                _ => unreachable!("migration asset pairing was validated above"),
            };
        Ok(DownloadedFirmware {
            path: path.to_string_lossy().into_owned(),
            signature_path: signature_path.to_string_lossy().into_owned(),
            file_name: asset.name,
            version_tag: tag,
            firmware_version: DownloadedFirmwareVersion {
                major: version_bytes[0],
                minor: version_bytes[1],
                patch: version_bytes[2],
            },
            migration_path,
            migration_signature_path,
        })
    })
    .await
    .map_err(|error| format!("firmware download worker failed: {error}"))?
}

#[tauri::command]
pub async fn kbhe_download_and_run_app_installer(tag: String) -> Result<String, String> {
    let launch_guard = AppInstallerLaunchGuard::acquire()?;
    tauri::async_runtime::spawn_blocking(move || {
        let mut launch_guard = launch_guard;
        let version = parse_prefixed_version(&tag, APP_TAG_PREFIX)
            .ok_or_else(|| format!("invalid app release tag: {tag}"))?;
        let current = Version::parse(env!("CARGO_PKG_VERSION"))
            .map_err(|error| format!("invalid built-in app version: {error}"))?;
        if version <= current {
            return Err(format!(
                "refusing app rollback/replay from {current} to {version}"
            ));
        }
        let (asset, signature_asset) =
            release_assets_by_tag(&tag, APP_TAG_PREFIX, installer_asset)?;
        let directory = secure_download_directory("app-update")?;
        let path = download_asset(&asset, &directory, MAX_APP_INSTALLER_BYTES)?;
        let signature_path = download_asset(&signature_asset, &directory, ED25519_SIGNATURE_BYTES)?;
        let mut installer_file = lock_verified_installer(&path)
            .map_err(|error| format!("failed to lock downloaded installer: {error}"))?;
        installer_file
            .seek(SeekFrom::Start(0))
            .map_err(|error| error.to_string())?;
        let mut installer = Vec::new();
        installer_file
            .read_to_end(&mut installer)
            .map_err(|error| format!("failed to read downloaded installer: {error}"))?;
        let signature = std::fs::read(&signature_path)
            .map_err(|error| format!("failed to read installer signature: {error}"))?;
        if let Err(error) = verify_app_asset(
            &installer,
            &version.to_string(),
            std::env::consts::OS,
            std::env::consts::ARCH,
            &asset.name,
            &signature,
        ) {
            let _ = std::fs::remove_file(&path);
            let _ = std::fs::remove_file(&signature_path);
            return Err(format!("downloaded installer is not authentic: {error}"));
        }

        #[cfg(target_os = "windows")]
        {
            let is_msi = path
                .extension()
                .and_then(|extension| extension.to_str())
                .is_some_and(|extension| extension.eq_ignore_ascii_case("msi"));

            if is_msi {
                Command::new("msiexec")
                    .arg("/i")
                    .arg(&path)
                    .spawn()
                    .map_err(|error| error.to_string())?;
            } else {
                Command::new(&path)
                    .spawn()
                    .map_err(|error| error.to_string())?;
            }
        }

        #[cfg(target_os = "macos")]
        Command::new("open")
            .arg(&path)
            .spawn()
            .map_err(|error| error.to_string())?;

        #[cfg(all(unix, not(target_os = "macos")))]
        Command::new("xdg-open")
            .arg(&path)
            .spawn()
            .map_err(|error| error.to_string())?;

        // Keep the no-write/no-delete Windows handle alive while the external
        // installer consumes the verified path. The configurator normally
        // exits for the update, so this bounded one-handle leak closes the
        // verify/execute replacement window without deleting an in-use MSI.
        std::mem::forget(installer_file);
        launch_guard.mark_launched();

        Ok(path.to_string_lossy().into_owned())
    })
    .await
    .map_err(|error| format!("app installer worker failed: {error}"))?
}

#[cfg(test)]
mod tests {
    use super::{
        firmware_asset, migration_asset, parse_prefixed_version, signature_asset, GithubAsset,
        APP_TAG_PREFIX, ED25519_SIGNATURE_BYTES, FIRMWARE_TAG_PREFIX,
    };

    fn asset(name: &str, size: u64) -> GithubAsset {
        GithubAsset {
            name: name.to_string(),
            browser_download_url: format!("https://example.invalid/{name}"),
            size,
        }
    }

    #[test]
    fn firmware_release_assets_are_selected_by_exact_role() {
        let assets = vec![
            asset("kbhe-app.bin.backup", 10),
            asset("kbhe-updater-v2-to-v3.bin.old", 10),
            asset("kbhe-app.bin", 100),
            asset("kbhe-app.bin.sig", ED25519_SIGNATURE_BYTES),
            asset("kbhe-updater-v2-to-v3.bin", 200),
            asset("kbhe-updater-v2-to-v3.bin.sig", ED25519_SIGNATURE_BYTES),
        ];
        let app = firmware_asset(&assets).unwrap();
        let migration = migration_asset(&assets).unwrap();
        assert_eq!(app.name, "kbhe-app.bin");
        assert_eq!(migration.name, "kbhe-updater-v2-to-v3.bin");
        assert_eq!(
            signature_asset(&assets, &app).unwrap().name,
            "kbhe-app.bin.sig"
        );
        assert_eq!(
            signature_asset(&assets, &migration).unwrap().name,
            "kbhe-updater-v2-to-v3.bin.sig"
        );
    }

    #[test]
    fn migration_signature_requires_exact_detached_size() {
        let migration = asset("kbhe-updater-v2-to-v3.bin", 200);
        let assets = vec![
            migration.clone(),
            asset("kbhe-updater-v2-to-v3.bin.sig", ED25519_SIGNATURE_BYTES + 1),
        ];
        assert!(signature_asset(&assets, &migration).is_none());
    }

    #[test]
    fn release_tags_are_canonical_and_stable() {
        assert_eq!(
            parse_prefixed_version("app-v1.2.3", APP_TAG_PREFIX)
                .unwrap()
                .to_string(),
            "1.2.3"
        );
        assert_eq!(
            parse_prefixed_version("firmware-v255.0.7", FIRMWARE_TAG_PREFIX)
                .unwrap()
                .to_string(),
            "255.0.7"
        );
        for tag in [
            "app-vv1.2.3",
            "app-v01.2.3",
            "app-v1.2.3-rc.1",
            "app-v1.2.3+build",
        ] {
            assert!(parse_prefixed_version(tag, APP_TAG_PREFIX).is_none());
        }
        assert!(parse_prefixed_version("firmware-v256.0.0", FIRMWARE_TAG_PREFIX).is_none());
    }
}
