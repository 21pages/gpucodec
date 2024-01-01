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
        .write_to_file(Path::new(&env::var_os("OUT_DIR").unwrap()).join("mfdx_ffi.rs"))
        .unwrap();

    // link
    [
        "kernel32",
        "user32",
        "gdi32",
        "winspool",
        "shell32",
        "ole32",
        "oleaut32",
        "uuid",
        "comdlg32",
        "advapi32",
        "d3d11",
        "dxgi",
        "mf",
        "mfplat",
        "mfuuid",
        "mfreadwrite",
        "wmcodecdspuuid",
    ]
    .map(|lib| println!("cargo:rustc-link-lib={}", lib));

    let mut builder = Build::new();
    builder
        .include("../common/src")
        .file("src/decode.cpp")
        .cpp(false)
        .warnings(false)
        .compile("mfdx");
}
