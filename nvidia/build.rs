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
    let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let externals_dir = manifest_dir.parent().unwrap().join("externals");

    // system
    #[cfg(target_os = "windows")]
    [
        "kernel32", "user32", "gdi32", "winspool", "shell32", "ole32", "oleaut32", "uuid",
        "comdlg32", "advapi32",
    ]
    .map(|lib| println!("cargo:rustc-link-lib={}", lib));

    // cuda
    #[cfg(windows)]
    {
        let cuda_path = externals_dir.join("CUDA").join("win_v11.6");
        builder.include(cuda_path.join("include"));
        #[cfg(target_arch = "x86_64")]
        let arch_dir = "x64";
        #[cfg(target_arch = "x86")]
        let arch_dir = "Win32";
        println!(
            "cargo:rustc-link-search={}",
            cuda_path.join("lib").join(arch_dir).display()
        );
        ["cudart_static", "cuda"].map(|lib| println!("cargo:rustc-link-lib={}", lib));
    }

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
    #[cfg(windows)]
    {
        #[cfg(target_arch = "x86_64")]
        let arch_dir = "x64";
        #[cfg(target_arch = "x86")]
        let arch_dir = "Win32";
        println!(
            "cargo:rustc-link-search={}",
            sdk_path.join("Lib").join(arch_dir).display()
        );
        ["nvcuvid", "nvencodeapi"].map(|lib| println!("cargo:rustc-link-lib={}", lib));
    }
    for file in vec!["NvEncoder.cpp", "NvEncoderCuda.cpp"] {
        builder.file(
            sdk_path
                .join("Samples")
                .join("NvCodec")
                .join("NVEncoder")
                .join(file),
        );
    }
    for file in vec!["NvDecoder.cpp"] {
        builder.file(
            sdk_path
                .join("Samples")
                .join("NvCodec")
                .join("NVDecoder")
                .join(file),
        );
    }

    // crate
    builder.include("../common/src");
    builder
        .file("src/encode.cpp")
        .file("src/decode.cpp")
        .cpp(false)
        .compile("nvidia");
}
