use cc::Build;
use std::{env, path::Path};

fn main() {
    println!("cargo:rerun-if-changed=src");
    bindgen::builder()
        .header("src/ffi.h")
        .rustified_enum(".*")
        .generate()
        .unwrap()
        .write_to_file(Path::new(&env::var_os("OUT_DIR").unwrap()).join("codec_ffi.rs"))
        .unwrap();

    let mut builder = Build::new();

    // crate
    builder
        .file("src/utils.c")
        .file("src/data.c")
        .cpp(false)
        .compile("gpu_video_codec");
}
