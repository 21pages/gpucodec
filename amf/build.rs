use cc::Build;
use std::{
    env,
    path::{Path, PathBuf},
};

fn main() {
    let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let externals_dir = manifest_dir.parent().unwrap().join("externals");
    let common_dir = manifest_dir.parent().unwrap().join("common");
    println!("cargo:rerun-if-changed=src");
    println!("cargo:rerun-if-changed={}", externals_dir.display());
    println!("cargo:rerun-if-changed={}", common_dir.display());
    bindgen::builder()
        .header("src/ffi.h")
        .rustified_enum("*")
        .generate()
        .unwrap()
        .write_to_file(Path::new(&env::var_os("OUT_DIR").unwrap()).join("amf_ffi.rs"))
        .unwrap();

    let mut builder = Build::new();

    // system
    #[cfg(windows)]
    println!("cargo:rustc-link-lib=ole32");
    #[cfg(target_os = "linux")]
    println!("cargo:rustc-link-lib=stdc++");

    // amf
    let amf_path = externals_dir.join("AMF_v1.4.29");
    builder.include(format!("{}/amf/public/common", amf_path.display()));
    builder.include(amf_path.join("amf"));
    for f in vec![
        "AMFFactory.cpp",
        "AMFSTL.cpp",
        "Thread.cpp",
        #[cfg(windows)]
        "Windows/ThreadWindows.cpp",
        #[cfg(target_os = "linux")]
        "Linux/ThreadLinux.cpp",
        "TraceAdapter.cpp",
    ] {
        builder.file(format!("{}/amf/public/common/{}", amf_path.display(), f));
    }

    // crate
    builder
        .include(manifest_dir.parent().unwrap().join("common").join("src"))
        .file("src/encode.cpp")
        .file("src/decode.cpp")
        .cpp(false)
        .warnings(false)
        .compile("amf");
}
