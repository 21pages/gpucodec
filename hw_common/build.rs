use std::{env, path::Path};

fn main() {
    println!("cargo:rerun-if-changed=src");
    bindgen::builder()
        .header("src/common.h")
        .header("src/callback.h")
        .rustified_enum("*")
        .parse_callbacks(Box::new(MyCallbacks))
        .generate()
        .unwrap()
        .write_to_file(Path::new(&env::var_os("OUT_DIR").unwrap()).join("common_ffi.rs"))
        .unwrap();
}

#[derive(Debug)]
struct MyCallbacks;
impl bindgen::callbacks::ParseCallbacks for MyCallbacks {
    fn add_derives(&self, name: &str) -> Vec<String> {
        let names = vec!["DataFormat", "HWDeviceType", "PixelFormat"];
        if names.contains(&name) {
            vec!["Serialize", "Deserialize"]
                .drain(..)
                .map(|s| s.to_string())
                .collect()
        } else {
            vec![]
        }
    }
}
