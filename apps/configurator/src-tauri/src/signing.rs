use ed25519_dalek::{Signature, VerifyingKey};
use sha2::{Digest, Sha512};

const FIRMWARE_CONTEXT: &[u8; 8] = b"KBHEFW3\0";
const APP_CONTEXT: &[u8; 8] = b"KBHEAPP2";
const SIGNATURE_SIZE: usize = 64;
const RELEASE_PUBLIC_KEY: [u8; 32] = [
    0x52, 0xF4, 0x0D, 0x13, 0xB9, 0xEF, 0x87, 0x39, 0xE1, 0xB9, 0x90, 0x1F, 0x72, 0x2B, 0x4E, 0x2A,
    0xFF, 0x58, 0x5F, 0xE6, 0xF6, 0x7C, 0xF1, 0xB6, 0x2A, 0x8D, 0xC3, 0xAE, 0x5B, 0xE7, 0x53, 0xAD,
];

fn crc32(data: &[u8]) -> u32 {
    let mut crc = 0xFFFF_FFFFu32;
    for &byte in data {
        crc ^= u32::from(byte);
        for _ in 0..8 {
            crc = if crc & 1 != 0 {
                (crc >> 1) ^ 0xEDB8_8320
            } else {
                crc >> 1
            };
        }
    }
    crc ^ 0xFFFF_FFFF
}

pub(crate) fn firmware_manifest(data: &[u8], version: [u8; 3]) -> Result<Vec<u8>, String> {
    let image_size = u32::try_from(data.len())
        .map_err(|_| "firmware image length does not fit the signed format".to_string())?;
    let digest = Sha512::digest(data);
    let mut manifest = Vec::with_capacity(84);
    manifest.extend_from_slice(FIRMWARE_CONTEXT);
    manifest.extend_from_slice(&image_size.to_le_bytes());
    manifest.extend_from_slice(&crc32(data).to_le_bytes());
    manifest.extend_from_slice(&[version[0], version[1], version[2], 0]);
    manifest.extend_from_slice(&digest);
    debug_assert_eq!(manifest.len(), 84);
    Ok(manifest)
}

fn push_manifest_field(manifest: &mut Vec<u8>, value: &str, label: &str) -> Result<(), String> {
    let bytes = value.as_bytes();
    if bytes.is_empty() || bytes.len() > usize::from(u16::MAX) || bytes.contains(&0) {
        return Err(format!("{label} must contain 1..65535 non-NUL UTF-8 bytes"));
    }
    manifest.extend_from_slice(&(bytes.len() as u16).to_le_bytes());
    manifest.extend_from_slice(bytes);
    Ok(())
}

fn app_manifest(
    data: &[u8],
    version: &str,
    platform: &str,
    arch: &str,
    role: &str,
) -> Result<Vec<u8>, String> {
    let digest = Sha512::digest(data);
    let mut manifest =
        Vec::with_capacity(96 + version.len() + platform.len() + arch.len() + role.len());
    manifest.extend_from_slice(APP_CONTEXT);
    push_manifest_field(&mut manifest, version, "app version")?;
    push_manifest_field(&mut manifest, platform, "app platform")?;
    push_manifest_field(&mut manifest, arch, "app architecture")?;
    push_manifest_field(&mut manifest, role, "app asset role")?;
    manifest.extend_from_slice(&(data.len() as u64).to_le_bytes());
    manifest.extend_from_slice(&digest);
    Ok(manifest)
}

fn verify_manifest(manifest: &[u8], signature_bytes: &[u8]) -> Result<(), String> {
    if signature_bytes.len() != SIGNATURE_SIZE {
        return Err(format!(
            "release signature has {} bytes; expected {SIGNATURE_SIZE}",
            signature_bytes.len()
        ));
    }
    let signature = Signature::from_slice(signature_bytes)
        .map_err(|error| format!("invalid Ed25519 signature: {error}"))?;
    let public_key = VerifyingKey::from_bytes(&RELEASE_PUBLIC_KEY)
        .map_err(|error| format!("invalid embedded release key: {error}"))?;
    public_key
        .verify_strict(manifest, &signature)
        .map_err(|_| "release signature verification failed".to_string())
}

pub fn verify_firmware_asset(
    data: &[u8],
    version: [u8; 3],
    signature: &[u8],
) -> Result<(), String> {
    verify_manifest(&firmware_manifest(data, version)?, signature)
}

pub fn verify_app_asset(
    data: &[u8],
    version: &str,
    platform: &str,
    arch: &str,
    role: &str,
    signature: &[u8],
) -> Result<(), String> {
    verify_manifest(
        &app_manifest(data, version, platform, arch, role)?,
        signature,
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    const VECTORS: &str = include_str!("../../../../firmware/tests/release_signing_vectors.json");

    fn decode_hex(value: &str) -> Vec<u8> {
        assert_eq!(value.len() % 2, 0);
        value
            .as_bytes()
            .chunks_exact(2)
            .map(|pair| {
                let pair = std::str::from_utf8(pair).unwrap();
                u8::from_str_radix(pair, 16).unwrap()
            })
            .collect()
    }

    #[test]
    fn python_generated_golden_vectors_match_and_verify() {
        let vectors: serde_json::Value = serde_json::from_str(VECTORS).unwrap();
        assert_eq!(
            decode_hex(vectors["publicKeyHex"].as_str().unwrap()),
            RELEASE_PUBLIC_KEY
        );

        let firmware = &vectors["firmware"];
        let firmware_data = decode_hex(firmware["dataHex"].as_str().unwrap());
        let version = firmware["version"]
            .as_str()
            .unwrap()
            .split('.')
            .map(|part| part.parse::<u8>().unwrap())
            .collect::<Vec<_>>();
        let firmware_version = [version[0], version[1], version[2]];
        let firmware_signature = decode_hex(firmware["signatureHex"].as_str().unwrap());
        let rust_firmware_manifest = firmware_manifest(&firmware_data, firmware_version).unwrap();
        assert_eq!(
            rust_firmware_manifest,
            decode_hex(firmware["manifestHex"].as_str().unwrap())
        );
        verify_firmware_asset(&firmware_data, firmware_version, &firmware_signature).unwrap();

        let app = &vectors["app"];
        let app_data = decode_hex(app["dataHex"].as_str().unwrap());
        let rust_app_manifest = app_manifest(
            &app_data,
            app["version"].as_str().unwrap(),
            app["platform"].as_str().unwrap(),
            app["arch"].as_str().unwrap(),
            app["role"].as_str().unwrap(),
        )
        .unwrap();
        assert_eq!(
            rust_app_manifest,
            decode_hex(app["manifestHex"].as_str().unwrap())
        );
        verify_app_asset(
            &app_data,
            app["version"].as_str().unwrap(),
            app["platform"].as_str().unwrap(),
            app["arch"].as_str().unwrap(),
            app["role"].as_str().unwrap(),
            &decode_hex(app["signatureHex"].as_str().unwrap()),
        )
        .unwrap();

        let mut tampered = firmware_data;
        tampered[10] ^= 1;
        assert!(verify_firmware_asset(&tampered, firmware_version, &firmware_signature).is_err());
    }

    #[test]
    fn malformed_signature_is_rejected() {
        let error = verify_firmware_asset(b"abc", [1, 0, 0], &[0; 63]).unwrap_err();
        assert!(error.contains("expected 64"));
    }
}
