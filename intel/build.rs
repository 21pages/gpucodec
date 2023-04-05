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
        .write_to_file(Path::new(&env::var_os("OUT_DIR").unwrap()).join("intel_ffi.rs"))
        .unwrap();

    let mut builder = Build::new();

    // system

    // ffnvcodec
    let ffnvcodec_path = externals_dir
        .join("nv-codec-headers_n11.1.5.2")
        .join("include")
        .join("ffnvcodec");
    builder.include(ffnvcodec_path);

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
            .files(["mfxloader.cpp", "mfxparser.cpp"].map(|f| mfx_path.join("linux").join("src")));
    }

    builder
        .includes([
            sdk_path.join("api").join("include"),
            sdk_path.join("tutorials").join("common"),
        ])
        .files(
            [
                "cmd_options.cpp",
                "common_utils.cpp",
                #[cfg(windows)]
                "common_utils_windows.cpp",
            ]
            .map(|f| sdk_path.join("tutorials").join("common").join(f)),
        );

    // crate
    builder
        .include("../hw_common/src")
        .file("src/encode.cpp")
        .file("src/decode.cpp")
        .cpp(false)
        .warnings(false)
        .define("NOMINMAX", "1")
        .compile("intel");
}
