use cc::Build;
use std::{
    env,
    path::{Path, PathBuf},
};

fn main() {
    let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let externals_dir = manifest_dir.parent().unwrap().join("externals");
    println!("cargo:rerun-if-changed=src");
    println!("cargo:rerun-if-changed={}", externals_dir.display());
    bindgen::builder()
        .header("src/ffi.h")
        .rustified_enum("*")
        .generate()
        .unwrap()
        .write_to_file(Path::new(&env::var_os("OUT_DIR").unwrap()).join("mux_ffi.rs"))
        .unwrap();

    let mut builder = Build::new();

    // system
    #[cfg(target_os = "windows")]
    ["User32", "bcrypt", "ole32", "advapi32"].map(|lib| println!("cargo:rustc-link-lib={}", lib));
    #[cfg(target_os = "linux")]
    ["va", "va-drm", "va-x11", "vdpau", "X11", "z"]
        .map(|lib| println!("cargo:rustc-link-lib={}", lib));

    // ffmpeg
    #[cfg(target_os = "windows")]
    let ffmpeg_path = externals_dir.join("ffmpeg").join("windows");
    #[cfg(target_os = "linux")]
    let ffmpeg_path = externals_dir.join("ffmpeg").join("linux");
    builder.include(ffmpeg_path.join("include"));
    println!(
        "cargo:rustc-link-search=native={}",
        ffmpeg_path.join("lib").display()
    );
    ["avcodec", "avutil", "avformat"].map(|lib| println!("cargo:rustc-link-lib=static={}", lib));

    // crate
    builder.file("src/mux.c").cpp(false).compile("mux");
}
