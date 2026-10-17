#!/usr/bin/env python3
"""Regenerate the original checker launch clip; FFmpeg is a maintainer tool only."""
import argparse
from pathlib import Path
import subprocess


def generate(output):
    # Original RGB pixels, matching the staged artwork. No input media/assets.
    # Match the 256x128 movie geometry of the pinned original channel reference.
    frame=bytes(channel for y in range(128) for x in range(256)
                for channel in ((0x1a,0x38,0x72) if (x//32+y//32)%2
                                else (0x2a,0x68,0xac)))
    output.parent.mkdir(parents=True,exist_ok=True)
    subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-nostdin','-n',
                    '-f','rawvideo','-pixel_format','rgb24','-video_size','256x128',
                    '-framerate','30','-i','pipe:0','-frames:v','60','-an',
                    '-c:v','libx264','-profile:v','baseline','-level:v','1.3',
                    '-pix_fmt','yuv420p','-threads','1',
                    '-x264-params','keyint=60:min-keyint=60:scenecut=0:ref=1:aud=1:force-cfr=1',
                    '-map_metadata','-1','-bsf:v','filter_units=remove_types=6',
                    '-f','h264',str(output)],input=frame*60,check=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    generate(parser.parse_args().output)
