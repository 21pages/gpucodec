#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

include!(concat!(env!("OUT_DIR"), "/nvidia_ffi.rs"));

use common::{DecodeCalls, EncodeCalls};

pub fn encode_calls() -> EncodeCalls {
    EncodeCalls {
        new: nvidia_new_encoder,
        encode: nvidia_encode,
        destroy: nvidia_destroy_encoder,
    }
}

pub fn decode_calls() -> DecodeCalls {
    DecodeCalls {
        new: nvidia_new_decoder,
        decode: nvidia_decode,
        destroy: nvidia_destroy_decoder,
    }
}
