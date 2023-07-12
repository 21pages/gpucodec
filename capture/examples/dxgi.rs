use capture::dxgi;
fn main() {
    unsafe {
        let mut capturer = dxgi::Capturer::new().unwrap();
        for _ in 0..1000 {
            let texture = capturer.capture(100);
            println!("is null:{}", texture.is_null());
        }
    }
}
