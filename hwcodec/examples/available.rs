use env_logger::{init_from_env, Env, DEFAULT_FILTER_ENV};
use hw_common::{DynamicContext, PresetContext};
use hwcodec::{decode, encode};

fn main() {
    init_from_env(Env::default().filter_or(DEFAULT_FILTER_ENV, "info"));

    println!("encoders:");
    let encoders = encode::available(
        PresetContext {
            width: 1920,
            height: 1080,
        },
        DynamicContext {
            kbitrate: 5000,
            framerate: 30,
        },
    );
    encoders.iter().map(|e| println!("{:?}", e)).count();
    println!("decoders:");
    let decoders = decode::available();
    decoders.iter().map(|e| println!("{:?}", e)).count();
}
