use cc::Build;
use std::{
    env,
    path::{Path, PathBuf},
};

fn main() {
    let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let externals_dir = manifest_dir.parent().unwrap().join("externals");
    let common_dir = manifest_dir.parent().unwrap().join("hw_common");
    println!("cargo:rerun-if-changed=src");
    println!("cargo:rerun-if-changed={}", externals_dir.display());
    println!("cargo:rerun-if-changed={}", common_dir.display());
    bindgen::builder()
        .header("src/ffi.h")
        .rustified_enum("*")
        .generate()
        .unwrap()
        .write_to_file(Path::new(&env::var_os("OUT_DIR").unwrap()).join("intel_ffi.rs"))
        .unwrap();

    let mut builder = Build::new();

    // system
    #[cfg(target_os = "linux")]
    println!("cargo:rustc-link-lib=stdc++");
    #[cfg(target_os = "linux")]
    {
        builder.include("/usr/include/drm");
        println!("cargo:rustc-link-lib=va");
        println!("cargo:rustc-link-lib=va-drm");
    }

    // libva-dev

    // MediaSDK
    let sdk_path = externals_dir.join("MediaSDK_22.5.4");

    // mfx_dispatch
    let mfx_path = sdk_path.join("api").join("mfx_dispatch");
    #[cfg(windows)]
    {
        builder
            .include(mfx_path.join("windows").join("include"))
            .files(
                [
                    "main.cpp",
                    "mfx_dispatcher.cpp",
                    "mfx_dxva2_device.cpp",
                    "mfx_library_iterator.cpp",
                    "mfx_load_plugin.cpp",
                    "mfx_win_reg_key.cpp",
                    "mfx_critical_section.cpp",
                    "mfx_dispatcher_log.cpp",
                    "mfx_driver_store_loader.cpp",
                    "mfx_function_table.cpp",
                    "mfx_load_dll.cpp",
                    "mfx_plugin_hive.cpp",
                ]
                .map(|f| mfx_path.join("windows").join("src").join(f)),
            );
    }
    #[cfg(target_os = "linux")]
    {
        builder
            .include(mfx_path.join("linux").join("include"))
            .files(["mfxloader.cpp", "mfxparser.cpp"].map(|f| mfx_path.join("linux").join(f)));
    }

    builder
        .define("DX11_D3D", None)
        // .define("DX9_D3D", None)
        .includes([
            sdk_path.join("api").join("include"),
            sdk_path.join("tutorials").join("common"),
        ])
        .files(
            [
                "common_utils.cpp",
                #[cfg(windows)]
                "common_utils_windows.cpp",
                #[cfg(windows)]
                "common_directx11.cpp",
                #[cfg(target_os = "linux")]
                "common_utils_linux.cpp",
                #[cfg(target_os = "linux")]
                "common_vaapi.cpp",
            ]
            .map(|f| sdk_path.join("tutorials").join("common").join(f)),
        );

    // crate
    #[cfg(target_os = "linux")]
    {
        // todo
        builder
            .define("MFX_MODULES_DIR", "\"/usr/lib/x86_64-linux-gnu\"")
            .define("MFX_PLUGINS_CONF_DIR", "\"/usr/share/mfx\"");
    }

    builder
        .include("../hw_common/src")
        .file("src/encode.cpp")
        .file("src/decode.cpp")
        .cpp(false)
        .warnings(false)
        .define("NOMINMAX", "1")
        .compile("intel");
}
