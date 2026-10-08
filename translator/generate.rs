//! Original generation-only driver for the separately installed, pinned lifter.
//! No upstream ABI implementation or desktop library is emitted.
use std::{env, fs, path::PathBuf};
use recomp3ds::{codegen, discover, rom::Title, Mods};

fn run() -> Result<(), String> {
    let args: Vec<_> = env::args_os().skip(1).collect();
    if args.len() != 2 { return Err("usage: ctr2wup-generate INPUT OUTPUT".into()); }
    let input = PathBuf::from(&args[0]);
    let output = PathBuf::from(&args[1]);
    // The caller owns atomic staging. Refuse to overwrite prior generated code.
    fs::create_dir(&output).map_err(|e| e.to_string())?;
    let title = Title::load(&input).map_err(|e| e.to_string())?;
    let programs = recomp3ds::programs(&title, Mods::default()).map_err(|e| e.to_string())?;
    let mut analyses: Vec<_> = programs.iter().map(|(_, p)| discover::analyze(p)).collect();
    // Every discovered instruction is a valid resume point in the checked ABI.
    // This permits exact budgets/fault PCs without relying on desktop blocks.
    for analysis in &mut analyses {
        for function in analysis.functions.values_mut() {
            function.labels.extend(function.instructions.iter().copied());
        }
    }
    let units: Vec<_> = programs.iter().zip(&analyses).enumerate().map(|(i, ((name, program), analysis))|
        codegen::Unit { module: (i != 0).then_some(name.as_str()), program, analysis }).collect();
    let mut sources = Vec::new();
    for (name, contents) in codegen::generate(&units, &[]) {
        if name == "recomp.h" { continue; }
        if name.ends_with(".c") { sources.push(format!("\"{name}\"")); }
        fs::write(output.join(name), contents).map_err(|e| e.to_string())?;
    }
    fs::write(output.join("generation.json"), format!(
        "{{\"schema_version\":1,\"abi\":4,\"source_commit\":\"f78cd3777a676a7c02538ab993ca574a6d843416\",\"program_id\":\"{:016x}\",\"modules\":{},\"sources\":[{}]}}\n",
        title.program_id(), units.len() - 1, sources.join(","))).map_err(|e| e.to_string())?;
    Ok(())
}
fn main() {
    if let Err(error) = run() { eprintln!("{error}"); std::process::exit(1); }
}
