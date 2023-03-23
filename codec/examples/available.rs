use codec::{decode, encode};
use env_logger::{init_from_env, Env, DEFAULT_FILTER_ENV};

fn main() {
    init_from_env(Env::default().filter_or(DEFAULT_FILTER_ENV, "info"));

    println!("encoders:");
    let encoders = encode::available();
    encoders.iter().map(|e| println!("{:?}", e)).count();
    println!("decoders:");
    let decoders = decode::available();
    decoders.iter().map(|e| println!("{:?}", e)).count();
}
