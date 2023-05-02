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
    let ffi_header = "src/render_ffi.h";
    bindgen::builder()
        .header(ffi_header)
        .rustified_enum("*")
        .generate()
        .unwrap()
        .write_to_file(Path::new(&env::var_os("OUT_DIR").unwrap()).join("render_ffi.rs"))
        .unwrap();

    let mut builder = Build::new();

    // system
    #[cfg(windows)]
    ["d3d11", "dxgi", "User32"].map(|lib| println!("cargo:rustc-link-lib={}", lib));
    #[cfg(target_os = "linux")]
    println!("cargo:rustc-link-lib=stdc++");

    #[cfg(windows)]
    {
        // dxgi
        // let ddd_path = externals_dir.join("DXGIDesktopDuplication").join("cpp");
        // builder.include(&ddd_path);
        // for f in vec![
        //     // "DesktopDuplication.cpp",
        //     "DisplayManager.cpp",
        //     "DuplicationManager.cpp",
        //     "OutputManager.cpp",
        //     "ThreadManager.cpp",
        // ] {
        //     builder.file(format!("{}/{}", ddd_path.display(), f));
        // }
        // builder.file(manifest_dir.join("src").join("dxgi.cpp"));

        builder.include("D:\\lib\\SDL\\include");
        builder.file(manifest_dir.join("src").join("dxgi_sdl.cpp"));
        println!("cargo:rustc-link-search=native=D:\\lib\\SDL\\lib\\x64");
        println!("cargo:rustc-link-lib=SDL2");
    }

    // crate
    builder.cpp(false).warnings(false).compile("render");
}
