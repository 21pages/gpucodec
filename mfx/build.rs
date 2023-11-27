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
        .write_to_file(Path::new(&env::var_os("OUT_DIR").unwrap()).join("mfx_ffi.rs"))
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
                    "mfx_dispatcher_main.cpp",
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

    let sample_path = sdk_path.join("samples").join("sample_common");
    builder
        .define("MFX_D3D11_SUPPORT", None)
        .includes([
            sdk_path.join("api").join("include"),
            sample_path.join("include"),
        ])
        .files(
            [
                "sample_utils.cpp",
                "base_allocator.cpp",
                #[cfg(windows)]
                "d3d11_allocator.cpp",
                "avc_bitstream.cpp",
                "avc_spl.cpp",
                "avc_nal_spl.cpp",
            ]
            .map(|f| sample_path.join("src").join(f)),
        )
        .files(
            [
                "time.cpp",
                "atomic.cpp",
                "shared_object.cpp",
                #[cfg(windows)]
                "thread_windows.cpp",
            ]
            .map(|f| sample_path.join("src").join("vm").join(f)),
        );

    let dxgi_path = externals_dir.join("nvEncDXGIOutputDuplicationSample");
    builder.include(&dxgi_path);

    // crate
    #[cfg(target_os = "linux")]
    {
        // todo
        builder
            .define("MFX_MODULES_DIR", "\"/usr/lib/x86_64-linux-gnu\"")
            .define("MFX_PLUGINS_CONF_DIR", "\"/usr/share/mfx\"");
    }

    builder
        .include("../common/src")
        .file("src/encode.cpp")
        .file("src/decode.cpp")
        .cpp(false)
        .warnings(false)
        .define("NOMINMAX", "1")
        .compile("intel");
}
