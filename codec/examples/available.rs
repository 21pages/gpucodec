use codec::encode;
use env_logger::{init_from_env, Env, DEFAULT_FILTER_ENV};

fn main() {
    init_from_env(Env::default().filter_or(DEFAULT_FILTER_ENV, "trace"));

    let encoders = encode::available_encoders();
    encoders.iter().map(|e| println!("{:?}", e)).count();
}
