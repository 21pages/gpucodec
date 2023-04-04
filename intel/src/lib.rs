use std::os::raw::c_int;

extern "C" {
    pub fn encode_main(argc: c_int, argv: *mut *mut i8);
}
