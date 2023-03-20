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

    println!("cargo:rerun-if-changed=src");
    println!("cargo:rerun-if-changed=AMF");

    #[cfg(target_os = "windows")]
    {
        let dyn_libs = ["ole32"];
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

    builder
        .includes(Some(PathBuf::from("AMF/amf")))
        .file("src/encode.cpp")
        .file("src/decode.cpp")
        .cpp(false)
        .compile("amf");
}
