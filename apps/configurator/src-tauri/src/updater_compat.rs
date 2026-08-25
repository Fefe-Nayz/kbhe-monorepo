use crate::signing::verify_firmware_asset;
use sha2::{Digest, Sha512};

pub(crate) const UPDATER_PROTOCOL_V2: u16 = 0x0002;
pub(crate) const UPDATER_PROTOCOL_V3: u16 = 0x0003;
pub(crate) const UPDATER_APP_BASE: u32 = 0x0801_0000;
pub(crate) const UPDATER_V2_APP_MAX_IMAGE_SIZE: u32 = 0x0004_FF00;
pub(crate) const UPDATER_V3_APP_MAX_IMAGE_SIZE: u32 = 0x0002_FF00;
pub(crate) const UPDATER_FLASH_WRITE_ALIGN: u32 = 4;
pub(crate) const UPDATER_FLAG_APP_VALID: u16 = 1 << 0;
pub(crate) const UPDATER_FLAG_SESSION_ACTIVE: u16 = 1 << 1;
pub(crate) const UPDATER_FLAG_SIGNATURE_REQUIRED: u16 = 1 << 2;
pub(crate) const UPDATER_KNOWN_FLAGS: u16 =
    UPDATER_FLAG_APP_VALID | UPDATER_FLAG_SESSION_ACTIVE | UPDATER_FLAG_SIGNATURE_REQUIRED;

pub(crate) const MIGRATION_DESCRIPTOR_MAGIC: &[u8; 8] = b"KBHEMIG3";
pub(crate) const MIGRATION_DESCRIPTOR_SIZE: usize = 128;
pub(crate) const MIGRATION_TARGET_ID: &[u8; 16] = b"KBHE75HEF723VET6";
pub(crate) const MIGRATION_FLAG_BOOTADDR_RESUMABLE: u32 = 1 << 0;
pub(crate) const MIGRATION_FLAG_V3_TRAILER_PRESEEDED: u32 = 1 << 1;
pub(crate) const MIGRATION_REQUIRED_FLAGS: u32 =
    MIGRATION_FLAG_BOOTADDR_RESUMABLE | MIGRATION_FLAG_V3_TRAILER_PRESEEDED;

const UPDATER_TRAILER_MAGIC: u32 = 0x5544_5452;
const UPDATER_V3_TRAILER_OFFSET: usize = UPDATER_V3_APP_MAX_IMAGE_SIZE as usize;
const UPDATER_V3_TRAILER_SIZE: usize = 84;
const MIGRATION_PACKAGE_SIZE: usize = UPDATER_V3_TRAILER_OFFSET + UPDATER_V3_TRAILER_SIZE;
const UPDATER_BOOTLOADER_BASE: u32 = 0x0800_0000;
const UPDATER_BOOTLOADER_MAX_SIZE: usize = 0x0000_C000;
const MIGRATOR_EXECUTABLE_MAX_SIZE: usize = 0x0001_0000;
const UPDATER_RAM_BASE: u32 = 0x2000_0000;
const UPDATER_RAM_END: u32 = 0x2003_FF00;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct UpdaterHello {
    pub protocol_version: u16,
    pub flags: u16,
    pub app_base: u32,
    pub app_max_size: u32,
    pub write_align: u32,
    pub installed_version: [u8; 3],
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct MigrationPackage {
    pub signed_image_size: usize,
    pub bootloader_offset: usize,
    pub bootloader_size: usize,
    pub version: [u8; 3],
    pub signature: [u8; 64],
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) enum FirmwareArtifact {
    Application,
    V2ToV3Migration(MigrationPackage),
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum FlashProtocol {
    SignedV3,
    LegacyMigrationV2,
}

impl FlashProtocol {
    pub fn requires_auth(self) -> bool {
        matches!(self, Self::SignedV3)
    }

    #[cfg(test)]
    pub fn is_migration(self) -> bool {
        matches!(self, Self::LegacyMigrationV2)
    }
}

fn read_u16(bytes: &[u8], offset: usize) -> Result<u16, String> {
    let raw = bytes
        .get(offset..offset + 2)
        .ok_or_else(|| "truncated updater compatibility record".to_string())?;
    Ok(u16::from_le_bytes([raw[0], raw[1]]))
}

fn read_u32(bytes: &[u8], offset: usize) -> Result<u32, String> {
    let raw = bytes
        .get(offset..offset + 4)
        .ok_or_else(|| "truncated updater compatibility record".to_string())?;
    Ok(u32::from_le_bytes([raw[0], raw[1], raw[2], raw[3]]))
}

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

fn vector_is_valid(image: &[u8], base: u32, max_end: u32) -> bool {
    let Ok(initial_sp) = read_u32(image, 0) else {
        return false;
    };
    let Ok(reset_handler) = read_u32(image, 4) else {
        return false;
    };
    let reset_address = reset_handler & !1;
    let image_end = base.saturating_add(image.len() as u32);
    initial_sp > UPDATER_RAM_BASE
        && initial_sp <= UPDATER_RAM_END
        && initial_sp & 7 == 0
        && reset_handler & 1 == 1
        && reset_address >= base
        && reset_address < image_end.min(max_end)
}

pub(crate) fn parse_updater_hello(payload: &[u8]) -> Result<UpdaterHello, String> {
    if payload.len() != 20 {
        return Err(format!(
            "UPDATER_HELLO_INVALID: HELLO returned {} bytes; expected exactly the common 20-byte v2/v3 schema",
            payload.len()
        ));
    }
    if payload[19] != 0 {
        return Err("UPDATER_HELLO_INVALID: HELLO reserved byte is non-zero".to_string());
    }
    Ok(UpdaterHello {
        protocol_version: read_u16(payload, 0)?,
        flags: read_u16(payload, 2)?,
        app_base: read_u32(payload, 4)?,
        app_max_size: read_u32(payload, 8)?,
        write_align: read_u32(payload, 12)?,
        installed_version: [payload[16], payload[17], payload[18]],
    })
}

fn parse_migration_package(firmware: &[u8]) -> Result<Option<MigrationPackage>, String> {
    if firmware.len() != MIGRATION_PACKAGE_SIZE {
        return Ok(None);
    }

    let trailer = &firmware[UPDATER_V3_TRAILER_OFFSET..];
    if read_u32(trailer, 0)? != UPDATER_TRAILER_MAGIC {
        return Ok(None);
    }
    if read_u32(trailer, 80)? != crc32(&trailer[..80]) {
        return Err(
            "UPDATER_MIGRATION_PACKAGE_INVALID: the pre-seeded v3 trailer CRC is invalid"
                .to_string(),
        );
    }
    let signed_image_size = read_u32(trailer, 4)? as usize;
    if signed_image_size < MIGRATION_DESCRIPTOR_SIZE
        || signed_image_size > UPDATER_V3_TRAILER_OFFSET
    {
        return Err(format!(
            "UPDATER_MIGRATION_PACKAGE_INVALID: signed migrator size {signed_image_size} is outside the v3 application slot"
        ));
    }
    if read_u32(trailer, 8)? != crc32(&firmware[..signed_image_size]) {
        return Err(
            "UPDATER_MIGRATION_PACKAGE_INVALID: signed migrator CRC does not match the package"
                .to_string(),
        );
    }
    if trailer[15] != 0 {
        return Err(
            "UPDATER_MIGRATION_PACKAGE_INVALID: the v3 trailer reserved byte is non-zero"
                .to_string(),
        );
    }
    if firmware[signed_image_size..UPDATER_V3_TRAILER_OFFSET]
        .iter()
        .any(|byte| *byte != 0xFF)
    {
        return Err(
            "UPDATER_MIGRATION_PACKAGE_INVALID: bytes between the signed migrator and v3 trailer must remain erased"
                .to_string(),
        );
    }

    let descriptor_offset = signed_image_size - MIGRATION_DESCRIPTOR_SIZE;
    let descriptor = &firmware[descriptor_offset..signed_image_size];
    if descriptor.get(..8) != Some(MIGRATION_DESCRIPTOR_MAGIC.as_slice()) {
        return Err(
            "UPDATER_MIGRATION_PACKAGE_INVALID: the signed migration descriptor is missing"
                .to_string(),
        );
    }
    if read_u16(descriptor, 8)? != 1
        || read_u16(descriptor, 10)? as usize != MIGRATION_DESCRIPTOR_SIZE
    {
        return Err(
            "UPDATER_MIGRATION_PACKAGE_INVALID: unsupported migration descriptor schema"
                .to_string(),
        );
    }
    if read_u16(descriptor, 12)? != UPDATER_PROTOCOL_V2
        || read_u16(descriptor, 14)? != UPDATER_PROTOCOL_V3
    {
        return Err(
            "UPDATER_MIGRATION_PACKAGE_INVALID: package is not an updater v2-to-v3 migration"
                .to_string(),
        );
    }
    let flags = read_u32(descriptor, 16)?;
    if flags != MIGRATION_REQUIRED_FLAGS {
        return Err(
            "UPDATER_MIGRATION_PACKAGE_INVALID: package migration flags are not the exact supported recovery contract"
                .to_string(),
        );
    }
    if descriptor.get(36..52) != Some(MIGRATION_TARGET_ID.as_slice()) {
        return Err(
            "UPDATER_MIGRATION_PACKAGE_INVALID: package targets different keyboard hardware"
                .to_string(),
        );
    }
    if read_u32(descriptor, 32)? as usize != signed_image_size {
        return Err(
            "UPDATER_MIGRATION_PACKAGE_INVALID: descriptor image size does not match the signed trailer"
                .to_string(),
        );
    }
    if read_u32(descriptor, 124)? != crc32(&descriptor[..124]) {
        return Err(
            "UPDATER_MIGRATION_PACKAGE_INVALID: migration descriptor CRC is invalid".to_string(),
        );
    }

    let bootloader_offset = read_u32(descriptor, 20)? as usize;
    let bootloader_size = read_u32(descriptor, 24)? as usize;
    let bootloader_end = bootloader_offset
        .checked_add(bootloader_size)
        .ok_or_else(|| {
            "UPDATER_MIGRATION_PACKAGE_INVALID: bootloader range overflows".to_string()
        })?;
    if bootloader_offset < 8
        || bootloader_offset > MIGRATOR_EXECUTABLE_MAX_SIZE
        || bootloader_offset & 3 != 0
        || bootloader_size == 0
        || bootloader_size > UPDATER_BOOTLOADER_MAX_SIZE
        || bootloader_end > descriptor_offset
    {
        return Err(
            "UPDATER_MIGRATION_PACKAGE_INVALID: embedded bootloader range is invalid".to_string(),
        );
    }
    let bootloader = &firmware[bootloader_offset..bootloader_end];
    if read_u32(descriptor, 28)? != crc32(bootloader) {
        return Err(
            "UPDATER_MIGRATION_PACKAGE_INVALID: embedded bootloader CRC does not match".to_string(),
        );
    }
    let digest = Sha512::digest(bootloader);
    if descriptor.get(52..116) != Some(digest.as_slice()) {
        return Err(
            "UPDATER_MIGRATION_PACKAGE_INVALID: embedded bootloader SHA-512 does not match"
                .to_string(),
        );
    }
    if descriptor[116..124].iter().any(|byte| *byte != 0) {
        return Err(
            "UPDATER_MIGRATION_PACKAGE_INVALID: migration descriptor reserved bytes are non-zero"
                .to_string(),
        );
    }
    if !vector_is_valid(
        &firmware[..signed_image_size],
        UPDATER_APP_BASE,
        UPDATER_APP_BASE + UPDATER_V3_APP_MAX_IMAGE_SIZE,
    ) {
        return Err(
            "UPDATER_MIGRATION_PACKAGE_INVALID: migrator vector table is invalid".to_string(),
        );
    }
    if !vector_is_valid(
        bootloader,
        UPDATER_BOOTLOADER_BASE,
        UPDATER_BOOTLOADER_BASE + UPDATER_BOOTLOADER_MAX_SIZE as u32,
    ) {
        return Err(
            "UPDATER_MIGRATION_PACKAGE_INVALID: embedded bootloader vector table is invalid"
                .to_string(),
        );
    }

    let mut signature = [0u8; 64];
    signature.copy_from_slice(&trailer[16..80]);
    Ok(Some(MigrationPackage {
        signed_image_size,
        bootloader_offset,
        bootloader_size,
        version: [trailer[12], trailer[13], trailer[14]],
        signature,
    }))
}

pub(crate) fn inspect_firmware_artifact(
    firmware: &[u8],
    version: [u8; 3],
    signature: &[u8],
) -> Result<FirmwareArtifact, String> {
    if let Some(package) = parse_migration_package(firmware)? {
        if package.version != version {
            return Err(format!(
                "UPDATER_MIGRATION_PACKAGE_INVALID: trailer version {}.{}.{} does not match selected version {}.{}.{}",
                package.version[0],
                package.version[1],
                package.version[2],
                version[0],
                version[1],
                version[2],
            ));
        }
        if signature != package.signature {
            return Err(
                "UPDATER_MIGRATION_PACKAGE_INVALID: detached signature does not match the signed v3 trailer"
                    .to_string(),
            );
        }
        verify_firmware_asset(
            &firmware[..package.signed_image_size],
            version,
            signature,
        )
        .map_err(|error| {
            format!(
                "UPDATER_MIGRATION_PACKAGE_INVALID: signed migrator authenticity check failed: {error}"
            )
        })?;
        return Ok(FirmwareArtifact::V2ToV3Migration(package));
    }

    if firmware.len() > UPDATER_V3_APP_MAX_IMAGE_SIZE as usize {
        return Err(format!(
            "UPDATER_IMAGE_TOO_LARGE: application has {} bytes; v3 maximum is {}. A legacy migration must use the exact signed migration-package layout.",
            firmware.len(),
            UPDATER_V3_APP_MAX_IMAGE_SIZE,
        ));
    }
    verify_firmware_asset(firmware, version, signature)
        .map_err(|error| format!("firmware authenticity check failed before flashing: {error}"))?;
    Ok(FirmwareArtifact::Application)
}

fn validate_updater_contract(hello: UpdaterHello) -> Result<FlashProtocol, String> {
    if hello.flags & !UPDATER_KNOWN_FLAGS != 0 {
        return Err(format!(
            "UPDATER_SECURITY_UNSUPPORTED: HELLO advertises unknown flags 0x{:04X}",
            hello.flags & !UPDATER_KNOWN_FLAGS
        ));
    }
    if hello.app_base != UPDATER_APP_BASE {
        return Err(format!(
            "UPDATER_GEOMETRY_UNSUPPORTED: updater app base is 0x{:08X}; expected 0x{UPDATER_APP_BASE:08X}",
            hello.app_base
        ));
    }
    if hello.write_align != UPDATER_FLASH_WRITE_ALIGN {
        return Err(format!(
            "UPDATER_GEOMETRY_UNSUPPORTED: flash write alignment is {}; expected {UPDATER_FLASH_WRITE_ALIGN}",
            hello.write_align
        ));
    }

    match hello.protocol_version {
        UPDATER_PROTOCOL_V3 => {
            if hello.app_max_size != UPDATER_V3_APP_MAX_IMAGE_SIZE {
                return Err(format!(
                    "UPDATER_GEOMETRY_UNSUPPORTED: protocol v3 reports max image {}; expected {UPDATER_V3_APP_MAX_IMAGE_SIZE}",
                    hello.app_max_size
                ));
            }
            if hello.flags & UPDATER_FLAG_SIGNATURE_REQUIRED == 0 {
                return Err(
                    "UPDATER_SECURITY_UNSUPPORTED: protocol v3 does not advertise device-side signature enforcement"
                        .to_string(),
                );
            }
            Ok(FlashProtocol::SignedV3)
        }
        UPDATER_PROTOCOL_V2 => {
            if hello.app_max_size != UPDATER_V2_APP_MAX_IMAGE_SIZE {
                return Err(format!(
                    "UPDATER_GEOMETRY_UNSUPPORTED: protocol v2 reports max image {}; expected {UPDATER_V2_APP_MAX_IMAGE_SIZE}",
                    hello.app_max_size
                ));
            }
            if hello.flags & UPDATER_FLAG_SIGNATURE_REQUIRED != 0 {
                return Err(
                    "UPDATER_SECURITY_UNSUPPORTED: protocol v2 returned an unknown signature-required flag combination"
                        .to_string(),
                );
            }
            Ok(FlashProtocol::LegacyMigrationV2)
        }
        protocol => Err(format!(
            "UPDATER_PROTOCOL_UNSUPPORTED: updater protocol 0x{protocol:04X} is not supported; supported protocols are 0x0002 (signed migration package only) and 0x0003"
        )),
    }
}

pub(crate) fn updater_cleanup_is_safe(hello: UpdaterHello) -> bool {
    validate_updater_contract(hello).is_ok()
}

pub(crate) fn negotiate_flash_protocol(
    hello: UpdaterHello,
    artifact: &FirmwareArtifact,
) -> Result<FlashProtocol, String> {
    let protocol = validate_updater_contract(hello)?;
    match protocol {
        FlashProtocol::SignedV3 => {
            if matches!(artifact, FirmwareArtifact::V2ToV3Migration(_)) {
                return Err(
                    "UPDATER_ARTIFACT_MISMATCH: this keyboard already has updater protocol v3; select the normal signed kbhe-app.bin image"
                        .to_string(),
                );
            }
        }
        FlashProtocol::LegacyMigrationV2 => {
            let FirmwareArtifact::V2ToV3Migration(package) = artifact else {
                return Err(
                    "UPDATER_MIGRATION_REQUIRED: this keyboard uses legacy updater protocol 0x0002. The normal firmware image is intentionally blocked because its sector-6 storage would invalidate the legacy updater on first boot. Use a stable release containing the signed kbhe-updater-v2-to-v3 migration pair, or perform the documented factory/ROM-DFU recovery."
                        .to_string(),
                );
            };
            let installed_is_known = hello.installed_version != [0, 0, 0];
            if installed_is_known && package.version < hello.installed_version {
                return Err(format!(
                    "UPDATER_ROLLBACK_REJECTED: migration version {}.{}.{} is older than the legacy updater's authenticated application {}.{}.{}",
                    package.version[0],
                    package.version[1],
                    package.version[2],
                    hello.installed_version[0],
                    hello.installed_version[1],
                    hello.installed_version[2],
                ));
            }
        }
    }
    Ok(protocol)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn put_u16(bytes: &mut [u8], offset: usize, value: u16) {
        bytes[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
    }

    fn put_u32(bytes: &mut [u8], offset: usize, value: u32) {
        bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
    }

    fn structural_migration_package() -> Vec<u8> {
        const IMAGE_SIZE: usize = 512;
        const BOOTLOADER_OFFSET: usize = 64;
        const BOOTLOADER_SIZE: usize = 64;
        let descriptor_offset = IMAGE_SIZE - MIGRATION_DESCRIPTOR_SIZE;
        let mut package = vec![0xFF; MIGRATION_PACKAGE_SIZE];

        put_u32(&mut package, 0, UPDATER_RAM_END);
        put_u32(&mut package, 4, UPDATER_APP_BASE + 9);
        put_u32(&mut package, BOOTLOADER_OFFSET, UPDATER_RAM_END);
        put_u32(
            &mut package,
            BOOTLOADER_OFFSET + 4,
            UPDATER_BOOTLOADER_BASE + 9,
        );

        package[descriptor_offset..descriptor_offset + 8]
            .copy_from_slice(MIGRATION_DESCRIPTOR_MAGIC);
        put_u16(&mut package, descriptor_offset + 8, 1);
        put_u16(
            &mut package,
            descriptor_offset + 10,
            MIGRATION_DESCRIPTOR_SIZE as u16,
        );
        put_u16(&mut package, descriptor_offset + 12, UPDATER_PROTOCOL_V2);
        put_u16(&mut package, descriptor_offset + 14, UPDATER_PROTOCOL_V3);
        put_u32(
            &mut package,
            descriptor_offset + 16,
            MIGRATION_REQUIRED_FLAGS,
        );
        put_u32(
            &mut package,
            descriptor_offset + 20,
            BOOTLOADER_OFFSET as u32,
        );
        put_u32(&mut package, descriptor_offset + 24, BOOTLOADER_SIZE as u32);
        let bootloader_crc =
            crc32(&package[BOOTLOADER_OFFSET..BOOTLOADER_OFFSET + BOOTLOADER_SIZE]);
        put_u32(&mut package, descriptor_offset + 28, bootloader_crc);
        put_u32(&mut package, descriptor_offset + 32, IMAGE_SIZE as u32);
        package[descriptor_offset + 36..descriptor_offset + 52]
            .copy_from_slice(MIGRATION_TARGET_ID);
        let bootloader_digest =
            Sha512::digest(&package[BOOTLOADER_OFFSET..BOOTLOADER_OFFSET + BOOTLOADER_SIZE]);
        package[descriptor_offset + 52..descriptor_offset + 116]
            .copy_from_slice(&bootloader_digest);
        package[descriptor_offset + 116..descriptor_offset + 124].fill(0);
        let descriptor_crc = crc32(&package[descriptor_offset..descriptor_offset + 124]);
        put_u32(&mut package, descriptor_offset + 124, descriptor_crc);

        let trailer = UPDATER_V3_TRAILER_OFFSET;
        put_u32(&mut package, trailer, UPDATER_TRAILER_MAGIC);
        put_u32(&mut package, trailer + 4, IMAGE_SIZE as u32);
        let image_crc = crc32(&package[..IMAGE_SIZE]);
        put_u32(&mut package, trailer + 8, image_crc);
        package[trailer + 12..trailer + 16].copy_from_slice(&[2, 0, 9, 0]);
        package[trailer + 16..trailer + 80].fill(0xA5);
        let trailer_crc = crc32(&package[trailer..trailer + 80]);
        put_u32(&mut package, trailer + 80, trailer_crc);
        package
    }

    fn hello(protocol: u16, flags: u16, max_size: u32) -> UpdaterHello {
        UpdaterHello {
            protocol_version: protocol,
            flags,
            app_base: UPDATER_APP_BASE,
            app_max_size: max_size,
            write_align: UPDATER_FLASH_WRITE_ALIGN,
            installed_version: [2, 0, 5],
        }
    }

    fn migration() -> FirmwareArtifact {
        FirmwareArtifact::V2ToV3Migration(MigrationPackage {
            signed_image_size: 1024,
            bootloader_offset: 256,
            bootloader_size: 128,
            version: [2, 0, 9],
            signature: [0xA5; 64],
        })
    }

    #[test]
    fn common_hello_schema_parses_v2_and_v3() {
        for protocol in [UPDATER_PROTOCOL_V2, UPDATER_PROTOCOL_V3] {
            let mut payload = [0u8; 20];
            payload[0..2].copy_from_slice(&protocol.to_le_bytes());
            payload[2..4].copy_from_slice(&UPDATER_FLAG_SIGNATURE_REQUIRED.to_le_bytes());
            payload[4..8].copy_from_slice(&UPDATER_APP_BASE.to_le_bytes());
            payload[8..12].copy_from_slice(&UPDATER_V3_APP_MAX_IMAGE_SIZE.to_le_bytes());
            payload[12..16].copy_from_slice(&UPDATER_FLASH_WRITE_ALIGN.to_le_bytes());
            payload[16..19].copy_from_slice(&[2, 0, 8]);
            let parsed = parse_updater_hello(&payload).unwrap();
            assert_eq!(parsed.protocol_version, protocol);
            assert_eq!(parsed.installed_version, [2, 0, 8]);
        }

        let mut reserved = [0u8; 20];
        reserved[19] = 1;
        assert!(parse_updater_hello(&reserved)
            .unwrap_err()
            .contains("reserved byte"));
        assert!(parse_updater_hello(&[0u8; 21])
            .unwrap_err()
            .contains("exactly"));
    }

    #[test]
    fn migration_package_parser_accepts_only_the_canonical_structure() {
        let package = structural_migration_package();
        let migration = parse_migration_package(&package).unwrap().unwrap();
        assert_eq!(migration.signed_image_size, 512);
        assert_eq!(migration.bootloader_offset, 64);
        assert_eq!(migration.bootloader_size, 64);
        assert_eq!(migration.version, [2, 0, 9]);
        assert_eq!(migration.signature, [0xA5; 64]);

        let mut unknown_flags = package.clone();
        let descriptor_offset = 512 - MIGRATION_DESCRIPTOR_SIZE;
        put_u32(
            &mut unknown_flags,
            descriptor_offset + 16,
            MIGRATION_REQUIRED_FLAGS | (1 << 31),
        );
        let descriptor_crc = crc32(&unknown_flags[descriptor_offset..descriptor_offset + 124]);
        put_u32(&mut unknown_flags, descriptor_offset + 124, descriptor_crc);
        let image_crc = crc32(&unknown_flags[..512]);
        put_u32(&mut unknown_flags, UPDATER_V3_TRAILER_OFFSET + 8, image_crc);
        let trailer_crc =
            crc32(&unknown_flags[UPDATER_V3_TRAILER_OFFSET..UPDATER_V3_TRAILER_OFFSET + 80]);
        put_u32(
            &mut unknown_flags,
            UPDATER_V3_TRAILER_OFFSET + 80,
            trailer_crc,
        );
        assert!(parse_migration_package(&unknown_flags)
            .unwrap_err()
            .contains("exact supported recovery contract"));
    }

    #[test]
    fn v3_accepts_only_normal_signed_application_flow() {
        let protocol = negotiate_flash_protocol(
            hello(
                UPDATER_PROTOCOL_V3,
                UPDATER_FLAG_SIGNATURE_REQUIRED,
                UPDATER_V3_APP_MAX_IMAGE_SIZE,
            ),
            &FirmwareArtifact::Application,
        )
        .unwrap();
        assert_eq!(protocol, FlashProtocol::SignedV3);
        assert!(protocol.requires_auth());

        let error = negotiate_flash_protocol(
            hello(
                UPDATER_PROTOCOL_V3,
                UPDATER_FLAG_SIGNATURE_REQUIRED,
                UPDATER_V3_APP_MAX_IMAGE_SIZE,
            ),
            &migration(),
        )
        .unwrap_err();
        assert!(error.contains("UPDATER_ARTIFACT_MISMATCH"));
    }

    #[test]
    fn v2_never_accepts_a_normal_application_image() {
        let error = negotiate_flash_protocol(
            hello(UPDATER_PROTOCOL_V2, 0, UPDATER_V2_APP_MAX_IMAGE_SIZE),
            &FirmwareArtifact::Application,
        )
        .unwrap_err();
        assert!(error.contains("UPDATER_MIGRATION_REQUIRED"));
        assert!(error.contains("sector-6"));
    }

    #[test]
    fn v2_accepts_only_the_validated_migration_plan_without_auth() {
        let protocol = negotiate_flash_protocol(
            hello(UPDATER_PROTOCOL_V2, 0, UPDATER_V2_APP_MAX_IMAGE_SIZE),
            &migration(),
        )
        .unwrap();
        assert_eq!(protocol, FlashProtocol::LegacyMigrationV2);
        assert!(!protocol.requires_auth());
        assert!(protocol.is_migration());
    }

    #[test]
    fn unknown_protocol_and_changed_geometry_fail_closed() {
        let unknown = negotiate_flash_protocol(
            hello(
                4,
                UPDATER_FLAG_SIGNATURE_REQUIRED,
                UPDATER_V3_APP_MAX_IMAGE_SIZE,
            ),
            &FirmwareArtifact::Application,
        )
        .unwrap_err();
        assert!(unknown.contains("UPDATER_PROTOCOL_UNSUPPORTED"));

        let changed = negotiate_flash_protocol(
            hello(UPDATER_PROTOCOL_V2, 0, UPDATER_V2_APP_MAX_IMAGE_SIZE - 4),
            &migration(),
        )
        .unwrap_err();
        assert!(changed.contains("UPDATER_GEOMETRY_UNSUPPORTED"));

        let unknown_flags = negotiate_flash_protocol(
            hello(
                UPDATER_PROTOCOL_V3,
                UPDATER_FLAG_SIGNATURE_REQUIRED | (1 << 15),
                UPDATER_V3_APP_MAX_IMAGE_SIZE,
            ),
            &FirmwareArtifact::Application,
        )
        .unwrap_err();
        assert!(unknown_flags.contains("UPDATER_SECURITY_UNSUPPORTED"));
    }

    #[test]
    fn cleanup_is_allowed_only_after_the_exact_known_contract() {
        assert!(updater_cleanup_is_safe(hello(
            UPDATER_PROTOCOL_V2,
            0,
            UPDATER_V2_APP_MAX_IMAGE_SIZE,
        )));
        assert!(updater_cleanup_is_safe(hello(
            UPDATER_PROTOCOL_V3,
            UPDATER_FLAG_SIGNATURE_REQUIRED,
            UPDATER_V3_APP_MAX_IMAGE_SIZE,
        )));

        assert!(!updater_cleanup_is_safe(hello(
            4,
            UPDATER_FLAG_SIGNATURE_REQUIRED,
            UPDATER_V3_APP_MAX_IMAGE_SIZE,
        )));
        assert!(!updater_cleanup_is_safe(hello(
            UPDATER_PROTOCOL_V2,
            0,
            UPDATER_V2_APP_MAX_IMAGE_SIZE - 4,
        )));
        assert!(!updater_cleanup_is_safe(hello(
            UPDATER_PROTOCOL_V3,
            UPDATER_FLAG_SIGNATURE_REQUIRED | (1 << 15),
            UPDATER_V3_APP_MAX_IMAGE_SIZE,
        )));
    }

    #[test]
    fn v2_migration_never_downgrades_a_known_installed_application() {
        let mut legacy = hello(UPDATER_PROTOCOL_V2, 0, UPDATER_V2_APP_MAX_IMAGE_SIZE);
        legacy.installed_version = [2, 1, 0];
        let error = negotiate_flash_protocol(legacy, &migration()).unwrap_err();
        assert!(error.contains("UPDATER_ROLLBACK_REJECTED"));

        legacy.installed_version = [2, 0, 9];
        assert_eq!(
            negotiate_flash_protocol(legacy, &migration()).unwrap(),
            FlashProtocol::LegacyMigrationV2
        );
    }
}
