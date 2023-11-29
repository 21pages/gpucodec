use capture::dxgi;
use env_logger::{init_from_env, Env, DEFAULT_FILTER_ENV};
use gpu_common::{
    DataFormat, DecodeContext, DecodeDriver, DynamicContext, EncodeContext, EncodeDriver,
    FeatureContext, API::*, MAX_GOP,
};
use gpucodec::{decode::Decoder, encode::Encoder};
use render::Render;
use std::{
    io::Write,
    path::PathBuf,
    time::{Duration, Instant},
};

fn main() {
    init_from_env(Env::default().filter_or(DEFAULT_FILTER_ENV, "trace"));
    let luid = 94758;
    unsafe {
        // one luid create render failed on my pc, wouldn't happen in rustdesk
        let output_shared_handle = false;
        let data_format = DataFormat::H264;
        let mut capturer = dxgi::Capturer::new().unwrap();
        let mut render = Render::new(luid, output_shared_handle).unwrap();

        let en_ctx = EncodeContext {
            f: FeatureContext {
                driver: EncodeDriver::MFX,
                api: API_DX11,
                data_format,
                luid,
            },
            d: DynamicContext {
                device: Some(capturer.device()),
                width: capturer.width(),
                height: capturer.height(),
                kbitrate: 5000,
                framerate: 30,
                gop: MAX_GOP as _,
                q_min: 12,
                q_max: 35,
            },
        };
        let de_ctx = DecodeContext {
            device: if output_shared_handle {
                None
            } else {
                Some(render.device())
            },
            driver: DecodeDriver::MFX,
            api: API_DX11,
            data_format,
            output_shared_handle,
            luid,
        };

        let mut enc = Encoder::new(en_ctx).unwrap();
        let mut dec = Decoder::new(de_ctx).unwrap();
        let filename = PathBuf::from(".\\1.264");
        let mut file = std::fs::File::create(filename).unwrap();
        let mut dup_sum = Duration::ZERO;
        let mut enc_sum = Duration::ZERO;
        let mut dec_sum = Duration::ZERO;
        loop {
            let start = Instant::now();
            let texture = capturer.capture(100);
            if texture.is_null() {
                continue;
            }
            dup_sum += start.elapsed();
            let start = Instant::now();
            let frame = enc.encode(texture).unwrap();
            enc_sum += start.elapsed();
            for f in frame {
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
