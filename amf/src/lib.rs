use std::ffi::c_int;

extern "C" {
    fn amf_encode() -> c_int;
}

mod test {
    use super::*;
    #[test]
    fn test() {
        unsafe {
            amf_encode();
        };
    }
}
