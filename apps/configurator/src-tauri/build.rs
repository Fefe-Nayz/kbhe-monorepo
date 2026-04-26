fn main() {
  // Ensure icon/config edits invalidate the build script output in dev builds.
  println!("cargo:rerun-if-changed=tauri.conf.json");
  println!("cargo:rerun-if-changed=icons");
  tauri_build::build()
}
