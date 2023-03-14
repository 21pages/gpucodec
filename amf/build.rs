use cc::Build;
use std::{
    env,
    path::{Path, PathBuf},
};

fn main() {
    bindgen::builder()
        .header("src/ffi.h")
        .rustified_enum("*")
        .generate()
        .unwrap()
        .write_to_file(Path::new(&env::var_os("OUT_DIR").unwrap()).join("amf_ffi.rs"))
        .unwrap();

    let mut builder = Build::new();

    // println!("rerun-if-changed=src");
    println!("rerun-if-changed=src/encode.cpp");
    println!("rerun-if-changed=src/ffi.h");
    println!("rerun-if-changed=AMF");

    #[cfg(target_os = "windows")]
    {
        let dyn_libs = ["User32", "bcrypt", "ole32", "advapi32"];
        dyn_libs.map(|lib| println!("cargo:rustc-link-lib={}", lib));
    }

    builder.include(format!("AMF/amf/public/common"));
    for f in vec![
        "AMFFactory.cpp",
        "AMFSTL.cpp",
        "Thread.cpp",
        #[cfg(target_os = "windows")]
        "Windows/ThreadWindows.cpp",
        "TraceAdapter.cpp",
    ] {
        builder.file(format!("AMF/amf/public/common/{}", f));
    }

    builder.include(format!("AMF/amf/public/samples/CPPSamples/common"));
    for f in vec![
        "CmdLogger.cpp",
        "EncoderParamsAV1.cpp",
        "EncoderParamsAVC.cpp",
        "EncoderParamsHEVC.cpp",
        "ParametersStorage.cpp",
    ] {
        builder.file(format!("AMF/amf/public/samples/CPPSamples/common/{}", f));
    }

    builder
        .includes(Some(PathBuf::from("AMF/amf")))
        .file("src/encode.cpp")
        .file("src/decode.cpp")
        .cpp(false)
        .compile("amf");
}
