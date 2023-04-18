use duplication::dxgi;
fn main() {
    unsafe {
        let mut dup = dxgi::Duplicator::new().unwrap();
        for _ in 0..1000 {
            let texture = dup.duplicate(100);
            println!("is null:{}", texture.is_null());
        }
    }
}
