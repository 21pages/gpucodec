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
    println!("cargo:rerun-if-changed={}", common_dir.display());
    println!("cargo:rerun-if-changed={}", externals_dir.display());
    bindgen::builder()
        .header("src/ffi.h")
        .rustified_enum("*")
        .generate()
        .unwrap()
        .write_to_file(Path::new(&env::var_os("OUT_DIR").unwrap()).join("nvidia_ffi.rs"))
        .unwrap();

    let mut builder = Build::new();

    // system
    #[cfg(target_os = "windows")]
    [
        "kernel32", "user32", "gdi32", "winspool", "shell32", "ole32", "oleaut32", "uuid",
        "comdlg32", "advapi32", "d3d11", "dxgi",
    ]
    .map(|lib| println!("cargo:rustc-link-lib={}", lib));
    #[cfg(target_os = "linux")]
    println!("cargo:rustc-link-lib=stdc++");

    // ffnvcodec
    let ffnvcodec_path = externals_dir
        .join("nv-codec-headers_n11.1.5.2")
        .join("include")
        .join("ffnvcodec");
    builder.include(ffnvcodec_path);

    // video codc sdk
    let sdk_path = externals_dir.join("Video_Codec_SDK_11.1.5");
    builder.includes([
        sdk_path.clone(),
        sdk_path.join("Interface"),
        sdk_path.join("Samples").join("Utils"),
        sdk_path.join("Samples").join("NvCodec"),
        sdk_path.join("Samples").join("NvCodec").join("NVEncoder"),
        sdk_path.join("Samples").join("NvCodec").join("NVDecoder"),
    ]);
    for file in vec!["NvEncoder.cpp", "NvEncoderCuda.cpp", "NvEncoderD3D11.cpp"] {
        builder.file(
            sdk_path
                .join("Samples")
                .join("NvCodec")
                .join("NvEncoder")
                .join(file),
        );
    }
    for file in vec!["NvDecoder.cpp"] {
        builder.file(
            sdk_path
                .join("Samples")
                .join("NvCodec")
                .join("NvDecoder")
                .join(file),
        );
    }

    let dxgi_path = externals_dir.join("nvEncDXGIOutputDuplicationSample");
    builder.include(&dxgi_path);

    // crate
    builder.include("../hw_common/src");
    // #[cfg(windows)]
    // builder.define("WIN32", None);
    builder
        .file("src/encode.cpp")
        .file("src/decode.cpp")
        .cpp(false)
        .warnings(false)
        .compile("nvidia");
}
