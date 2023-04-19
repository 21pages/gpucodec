#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

use std::os::raw::c_void;

include!(concat!(env!("OUT_DIR"), "/dup_ffi.rs"));

pub struct Duplicator {
    inner: Box<c_void>,
}

impl Duplicator {
    pub fn new() -> Result<Self, ()> {
        let inner = unsafe { dxgi_new_duplicator() };
        if inner.is_null() {
            Err(())
        } else {
            Ok(Self {
                inner: unsafe { Box::from_raw(inner) },
            })
        }
    }

    pub unsafe fn device(&mut self) -> *mut c_void {
        dxgi_device(self.inner.as_mut())
    }

    pub unsafe fn duplicate(&mut self, wait_ms: i32) -> *mut c_void {
        dxgi_duplicate(self.inner.as_mut(), wait_ms)
    }

    pub unsafe fn drop(&mut self) {
        destroy_dxgi_duplicator(self.inner.as_mut());
    }
}
