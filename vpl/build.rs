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
        .write_to_file(Path::new(&env::var_os("OUT_DIR").unwrap()).join("vpl_ffi.rs"))
        .unwrap();

    let mut builder = Build::new();

    let sdk_path = externals_dir.join("libvpl_v2023.4.0");

    // libvpl
    let libvpl_path = sdk_path.join("libvpl");
    let api_path = sdk_path.join("api");
    let examples_path = sdk_path.join("examples");

    builder
        .includes([&libvpl_path, &api_path, &examples_path])
        .files(
            [
                "mfx_dispatcher_vpl.cpp",
                "mfx_dispatcher_vpl_config.cpp",
                "mfx_dispatcher_vpl_loader.cpp",
                "mfx_dispatcher_vpl_log.cpp",
                "mfx_dispatcher_vpl_lowlatency.cpp",
                "mfx_dispatcher_vpl_msdk.cpp",
            ]
            .map(|f| libvpl_path.join("src").join(f)),
        )
        .files(
            [
                "main.cpp",
                "mfx_critical_section.cpp",
                "mfx_dispatcher.cpp",
                "mfx_dispatcher_log.cpp",
                "mfx_driver_store_loader.cpp",
                "mfx_dxva2_device.cpp",
                "mfx_function_table.cpp",
                "mfx_library_iterator.cpp",
                "mfx_load_dll.cpp",
                "mfx_win_reg_key.cpp",
            ]
            .map(|f| libvpl_path.join("src").join("windows").join(f)),
        )
        .files(
            [
                "mfx_config_interface.cpp",
                "mfx_config_interface_string_api.cpp",
            ]
            .map(|f| libvpl_path.join("src").join("mfx_config_interface").join(f)),
        );

    // link
    [
        "kernel32", "user32", "gdi32", "winspool", "shell32", "ole32", "oleaut32", "uuid",
        "comdlg32", "advapi32", "d3d11", "dxgi",
    ]
    .map(|lib| println!("cargo:rustc-link-lib={}", lib));

    builder
        .include("../common/src")
        .file("src/encode.cpp")
        .file("src/decode.cpp")
        .cpp(false)
        .warnings(false)
        .define("WIN32", "")
        .define("_WINDOWS", "")
        .define("MFX_DEPRECATED_OFF", "")
        .define("ONEVPL_EXPERIMENTAL", "")
        .define("NOMINMAX", "1")
        .compile("vpl");
}
