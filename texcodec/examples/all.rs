use duplication::dxgi;
use hw_common::{
    DataFormat, DecodeContext, DecodeDriver, DynamicContext, EncodeContext, EncodeDriver,
    FeatureContext, SurfaceFormat::*, API::*, MAX_GOP,
};
use render::Render;
use std::{
    io::Write,
    path::PathBuf,
    time::{Duration, Instant},
};
use texcodec::{decode::Decoder, encode::Encoder};

fn main() {
    unsafe {
        // one luid create render failed on my pc, wouldn't happen in rustdesk
        let luid = 64352;
        let data_format = DataFormat::H265;
        let mut dup = dxgi::Duplicator::new().unwrap();
        let mut render = Render::new(luid).unwrap();

        let en_ctx = EncodeContext {
            f: FeatureContext {
                driver: EncodeDriver::AMF,
                api: API_DX11,
                data_format,
                luid,
            },
            d: DynamicContext {
                device: Some(dup.device()),
                width: 2880,
                height: 1800,
                kbitrate: 5000,
                framerate: 30,
                gop: MAX_GOP as _,
            },
        };
        let de_ctx = DecodeContext {
            driver: DecodeDriver::AMF,
            api: API_DX11,
            data_format,
            output_surface_format: SURFACE_FORMAT_BGRA,
            luid,
        };

        let mut enc = Encoder::new(en_ctx).unwrap();
        let mut dec = Decoder::new(de_ctx).unwrap();
        let filename = PathBuf::from("D:\\tmp\\1.264");
        let mut file = std::fs::File::create(filename).unwrap();
        let mut dup_sum = Duration::ZERO;
        let mut enc_sum = Duration::ZERO;
        let mut dec_sum = Duration::ZERO;
        loop {
            let start = Instant::now();
            let texture = dup.duplicate(100);
            if texture.is_null() {
                println!("texture is null");
                continue;
            }
            dup_sum += start.elapsed();
            let start = Instant::now();
            let frame = enc.encode(texture).unwrap();
            enc_sum += start.elapsed();
            for f in frame {
                println!("len:{}", f.data.len());
                file.write_all(&mut f.data).unwrap();
                let start = Instant::now();
                let frames = dec.decode(&f.data).unwrap();
                dec_sum += start.elapsed();
                for f in frames {
                    render.render(f.texture).unwrap();
                }
            }
        }
    }
}
