use std::ffi::{c_char, c_int, CString};

use intel::encode_main;

pub fn main() {
    let args = std::env::args()
        .map(|arg| CString::new(arg).unwrap())
        .collect::<Vec<CString>>();
    // convert the strings to raw pointers
    let c_args = args
        .iter()
        .map(|arg| arg.as_ptr())
        .collect::<Vec<*const c_char>>();
    unsafe {
        // pass the pointer of the vector's internal buffer to a C function
        encode_main(c_args.len() as c_int, c_args.as_ptr() as *mut *mut i8);
    };
}
