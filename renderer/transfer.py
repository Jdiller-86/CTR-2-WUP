"""Original checked reference for the scoped, unscaled display transfer."""
from .texture import decode_texture


def layout(input_dim,output_dim,flags):
    if any(type(v) is not int or not 0<=v<=0xffffffff for v in (input_dim,output_dim,flags)):
        raise ValueError('transfer words must be uint32')
    width,height=input_dim&65535,input_dim>>16
    inf,outf=(flags>>8)&7,(flags>>12)&7
    linear=bool(flags&2);tiled=linear!=bool(flags&32)
    if input_dim!=output_dim or not 1<=width<=1024 or not 1<=height<=1024 or inf>4 or outf>4 or flags&~0x7723:
        raise ValueError('unsupported transfer dimensions/format/flags')
    if (not linear or tiled) and (width%8 or height%8):
        raise ValueError('tiled dimensions must be multiples of eight')
    return width,height,inf,outf,linear,tiled,bool(flags&1)


def offset(x,y,width,tiled):
    if not tiled:return y*width+x
    small=sum(((x>>i)&1)*4**i+((y>>i)&1)*2*4**i for i in range(3))
    return ((y//8)*(width//8)+x//8)*64+small


def convert(data,input_dim,output_dim,flags):
    width,height,inf,outf,linear,tiled,flip=layout(input_dim,output_dim,flags)
    bpp=lambda f:4 if f==0 else 3 if f==1 else 2
    if len(data)<width*height*bpp(inf):raise ValueError('transfer source truncated')
    # Decode single packed pixels through the independently tested texture path;
    # repeat a pixel into an original tile rather than sharing the C decoder.
    mapping=(0,1,3,2,4);cache={};output=bytearray(width*height*bpp(outf))
    for y in range(height):
        for x in range(width):
            at=offset(x,y,width,not linear)*bpp(inf);word=bytes(data[at:at+bpp(inf)])
            rgba=cache.get(word)
            if rgba is None:
                rgba=decode_texture(word*64,mapping[inf],8,8)[:4];cache[word]=rgba
            r,g,b,a=rgba
            if outf==0:encoded=bytes((a,b,g,r))
            elif outf==1:encoded=bytes((b,g,r))
            elif outf==2:encoded=((r>>3)<<11|(g>>2)<<5|(b>>3)).to_bytes(2,'little')
            elif outf==3:encoded=((r>>3)<<11|(g>>3)<<6|(b>>3)<<1|(a>>7)).to_bytes(2,'little')
            else:encoded=((r>>4)<<12|(g>>4)<<8|(b>>4)<<4|(a>>4)).to_bytes(2,'little')
            dst=offset(x,height-1-y if flip else y,width,tiled)*bpp(outf)
            output[dst:dst+bpp(outf)]=encoded
    return bytes(output)
