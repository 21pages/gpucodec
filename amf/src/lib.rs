#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

include!(concat!(env!("OUT_DIR"), "/amf_ffi.rs"));

use common::{DecodeCalls, EncodeCalls};

pub fn encode_calls() -> EncodeCalls {
    EncodeCalls {
        new: amf_new_encoder,
        encode: amf_encode,
        destroy: amf_destroy_encoder,
    }
}

pub fn decode_calls() -> DecodeCalls {
    DecodeCalls {
        new: amf_new_decoder,
        decode: amf_decode,
        destroy: amf_destroy_decoder,
    }
}
