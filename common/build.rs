use std::{env, path::Path};

fn main() {
    println!("cargo:rerun-if-changed=src");
    bindgen::builder()
        .header("src/common.h")
        .header("src/callback.h")
        .rustified_enum("*")
        .generate()
        .unwrap()
        .write_to_file(Path::new(&env::var_os("OUT_DIR").unwrap()).join("common_ffi.rs"))
        .unwrap();
}
