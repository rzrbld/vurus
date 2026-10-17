#![allow(dead_code, unused_imports)]
#[path = "../../../../third_party/oxideav-indeo/src/indeo5/mod.rs"]
mod indeo5;
use std::{slice, panic::{catch_unwind, AssertUnwindSafe}};

// C++ owns all buffers. No Rust allocations or exceptions cross this boundary.
#[no_mangle]
pub unsafe extern "C" fn vyruz_indeo_decode(data: *const u8, len: usize,
    width: u32, height: u32, rgba: *mut u8, capacity: usize,
    error: *mut u8, error_capacity: usize) -> i32 {
    let result = catch_unwind(AssertUnwindSafe(|| -> Result<(), String> {
        if data.is_null() || rgba.is_null() || width == 0 || height == 0 ||
            width > 2048 || height > 2048 || capacity < width as usize * height as usize * 4 {
            return Err("Invalid decoder buffers or dimensions".into());
        }
        let picture = indeo5::decode_intra_picture(slice::from_raw_parts(data,len))
            .map_err(|e| format!("Indeo5: {e:?}"))?;
        if picture.header.dimensions() != Some((width,height)) || !picture.parse_complete || !picture.frontiers.is_empty() {
            return Err(format!("Incomplete/unsupported Indeo5 picture: dimensions={:?} complete={} frontiers={:?}", picture.header.dimensions(),picture.parse_complete,picture.frontiers));
        }
        let divisor = match picture.format {
            Some(indeo5::OutputFormat::Yvu9) => 4,
            Some(indeo5::OutputFormat::Yv12) => 2,
            _ => return Err("Unsupported Indeo5 output format".into())
        };
        let output = picture.output.ok_or("Indeo5 produced no picture")?;
        let luma = output.plane_bytes(indeo5::PlaneRole::Luma);
        let u = output.plane_bytes(indeo5::PlaneRole::ChromaU);
        let v = output.plane_bytes(indeo5::PlaneRole::ChromaV);
        let (w,h) = (width as usize,height as usize);
        let cw = (w+divisor-1)/divisor;
        if luma.len() != w*h || u.len() < cw*((h+divisor-1)/divisor) || v.len()!=u.len() {
            return Err("Invalid decoded plane sizes".into());
        }
        let pixels = slice::from_raw_parts_mut(rgba,w*h*4);
        for y in 0..h {for x in 0..w {
            let p = y*w+x;let c = (y/divisor)*cw+x/divisor;
            // BT.601 limited-range conversion. Original codec RGB LUT is not
            // available; this is the standard renderer conversion from YUV.
            let yy = i32::from(luma[p])-16;let uu=i32::from(u[c])-128;let vv=i32::from(v[c])-128;
            pixels[4*p] = ((298*yy+409*vv+128)>>8).clamp(0,255) as u8;
            pixels[4*p+1] = ((298*yy-100*uu-208*vv+128)>>8).clamp(0,255) as u8;
            pixels[4*p+2] = ((298*yy+516*uu+128)>>8).clamp(0,255) as u8;
            pixels[4*p+3] = 255;
        }}
        Ok(())
    }));
    let message = match result {Ok(Ok(())) => return 1, Ok(Err(e))=>e, Err(_)=>"Indeo5 decoder rejected malformed input".into()};
    if !error.is_null() && error_capacity>0 {
        let out=slice::from_raw_parts_mut(error,error_capacity);
        let n=message.len().min(out.len()-1);out[..n].copy_from_slice(&message.as_bytes()[..n]);out[n]=0;
    }
    0
}
