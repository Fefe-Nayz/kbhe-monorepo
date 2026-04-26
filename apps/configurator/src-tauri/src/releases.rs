use reqwest::blocking::Client;
use semver::Version;
use serde::{Deserialize, Serialize};
use std::fs::File;
use std::io::{copy, Write};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::Duration;

const APP_TAG_PREFIX: &str = "app-v";
const FIRMWARE_TAG_PREFIX: &str = "firmware-v";

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
    file_name: String,
    version_tag: String,
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
        "https://api.github.com/repos/{}/{}/releases",
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
    Version::parse(raw.trim_start_matches('v')).ok()
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
        [".AppImage", ".deb", ".rpm"].as_slice()
    };

    for ext in preferred_ext {
        if let Some(asset) = assets
            .iter()
            .find(|asset| asset.name.to_lowercase().ends_with(ext))
        {
            return Some(asset.clone());
        }
    }

    assets.first().cloned()
}

fn firmware_asset(assets: &[GithubAsset]) -> Option<GithubAsset> {
    assets
        .iter()
        .find(|asset| asset.name == "kbhe-app.bin")
        .or_else(|| {
            assets
                .iter()
                .find(|asset| asset.name.to_lowercase().ends_with(".bin"))
        })
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

fn download_asset(asset: &GithubAsset, directory: &Path) -> Result<PathBuf, String> {
    std::fs::create_dir_all(directory).map_err(|error| error.to_string())?;
    let destination = directory.join(sanitize_filename(&asset.name));
    let mut response = client()?
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

    let mut file = File::create(&destination).map_err(|error| error.to_string())?;
    copy(&mut response, &mut file).map_err(|error| error.to_string())?;
    file.flush().map_err(|error| error.to_string())?;
    Ok(destination)
}

fn release_asset_by_tag(
    tag: &str,
    prefix: &str,
    pick_asset: fn(&[GithubAsset]) -> Option<GithubAsset>,
) -> Result<GithubAsset, String> {
    if parse_prefixed_version(tag, prefix).is_none() {
        return Err(format!("unexpected release tag for {prefix}: {tag}"));
    }

    fetch_releases()?
        .into_iter()
        .find(|release| release.tag_name == tag && !release.draft)
        .and_then(|release| pick_asset(&release.assets))
        .ok_or_else(|| format!("no downloadable asset found for release {tag}"))
}

#[tauri::command]
pub async fn kbhe_download_firmware_release(tag: String) -> Result<DownloadedFirmware, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let asset = release_asset_by_tag(&tag, FIRMWARE_TAG_PREFIX, firmware_asset)?;
        let directory = std::env::temp_dir()
            .join("kbhe-configurator")
            .join("firmware");
        let path = download_asset(&asset, &directory)?;
        Ok(DownloadedFirmware {
            path: path.to_string_lossy().into_owned(),
            file_name: asset.name,
            version_tag: tag,
        })
    })
    .await
    .map_err(|error| format!("firmware download worker failed: {error}"))?
}

#[tauri::command]
pub async fn kbhe_download_and_run_app_installer(tag: String) -> Result<String, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let asset = release_asset_by_tag(&tag, APP_TAG_PREFIX, installer_asset)?;
        let directory = std::env::temp_dir()
            .join("kbhe-configurator")
            .join("app-update");
        let path = download_asset(&asset, &directory)?;

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

        Ok(path.to_string_lossy().into_owned())
    })
    .await
    .map_err(|error| format!("app installer worker failed: {error}"))?
}
