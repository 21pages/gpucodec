use cc::Build;
use std::{
    env,
    path::{Path, PathBuf},
};

fn main() {
    println!("cargo:rerun-if-changed=src");
    bindgen::builder()
        .header("src/ffi.h")
        .rustified_enum("*")
        .generate()
        .unwrap()
        .write_to_file(Path::new(&env::var_os("OUT_DIR").unwrap()).join("nvidia_ffi.rs"))
        .unwrap();

    let mut builder = Build::new();

    #[cfg(target_os = "windows")]
    [
        "kernel32", "user32", "gdi32", "winspool", "shell32", "ole32", "oleaut32", "uuid",
        "comdlg32", "advapi32",
    ]
    .map(|lib| println!("cargo:rustc-link-lib={}", lib));

    let cuda_path = PathBuf::from("C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.6");
    // let cuda_path = PathBuf::from("E:/ffmpeg/nv_sdk");
    builder.include(cuda_path.join("include"));
    println!(
        "cargo:rustc-link-search={}",
        cuda_path.join("lib").join("x64").display() // cuda_path.join("x64").display()
    );
    ["cudart_static", "cuda"].map(|lib| println!("cargo:rustc-link-lib={}", lib));

    let sdk_path = PathBuf::from("E:/native_codec/nvidia/Video_Codec_SDK_11.1.5");
    builder.includes([
        sdk_path.clone(),
        sdk_path.join("Interface"),
        sdk_path.join("Samples").join("Utils"),
        sdk_path.join("Samples").join("NvCodec"),
        sdk_path.join("Samples").join("NvCodec").join("NVEncoder"),
        sdk_path.join("Samples").join("NvCodec").join("NVDecoder"),
    ]);
    println!(
        "cargo:rustc-link-search={}",
        sdk_path.join("Lib").join("x64").display()
    );
    ["nvcuvid", "nvencodeapi"].map(|lib| println!("cargo:rustc-link-lib={}", lib));
    for file in vec![
        "NvEncoder.cpp",
        "NvEncoderCuda.cpp",
        // "NvEncoderOutputInVidMemCuda.cpp",
    ] {
        builder.file(
            sdk_path
                .join("Samples")
                .join("NvCodec")
                .join("NVEncoder")
                .join(file),
        );
    }
    for file in vec![
        "NvDecoder.cpp",
        // "NvEncoderOutputInVidMemCuda.cpp",
    ] {
        builder.file(
            sdk_path
                .join("Samples")
                .join("NvCodec")
                .join("NVDecoder")
                .join(file),
        );
    }
    // builder.file(sdk_path.join("Samples").join("Utils").join("crc.cu"));

    builder.include("../common/src");

    builder
        .file("src/encode.cpp")
        .file("src/decode.cpp")
        .cpp(false)
        .compile("nvidia");
}
