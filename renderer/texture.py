"""Original PICA base-texture reference; sampling/draw execution is separate."""
import struct

BITS = (32,24,16,16,16,16,16,8,8,8,4,4,4,8)
MODIFIERS = ((2,8),(5,17),(9,29),(13,42),(18,60),(24,80),(33,106),(47,183))


def required_bytes(format,width,height):
    if type(format) is not int or not 0 <= format < 14:
        raise ValueError('unsupported PICA texture format')
    if any(type(n) is not int or n<8 or n>1024 or n&(n-1) for n in (width,height)):
        raise ValueError('base texture dimensions must be powers of two in 8..1024')
    return width*height*BITS[format]//8


def expand(value,bits):
    if bits==4:return value*17
    return (value<<(8-bits)) | (value>>(2*bits-8))


def etc(data,x,y):
    low,high=struct.unpack('<II',data)
    delta=lambda n:n-8 if n&4 else n
    first=(y<2) if high&1 else (x<2)
    bases=[]
    for shift in (24,16,8):
        if high&2:
            base=(high>>(shift+3))&31
            second=base+delta((high>>shift)&7)
            if not 0<=second<=31:raise ValueError('ETC1 differential base is invalid (ETC2 modes unsupported)')
            bases.append(expand(base if first else second,5))
        else:bases.append(expand((high>>(shift+(4 if first else 0)))&15,4))
    index=x*4+y;modifier=MODIFIERS[(high>>(5 if first else 2))&7][(low>>index)&1]
    if (low>>(index+16))&1:modifier=-modifier
    return tuple(max(0,min(255,base+modifier)) for base in bases)+(255,)


def decode_texture(data,format,width,height):
    size=required_bytes(format,width,height)
    if len(data)<size:raise ValueError('texture source is truncated')
    result=bytearray(width*height*4)
    for y in range(height):
        for x in range(width):
            tile=(y//8)*(width//8)+x//8
            if format>=12:
                stride=8 if format==12 else 16
                block=(tile*4+(y%8)//4*2+(x%8)//4)*stride
                rgba=etc(data[block+(8 if format==13 else 0):block+stride],x%4,y%4)
                if format==13:
                    alpha=int.from_bytes(data[block:block+8],'little')
                    rgba=rgba[:3]+(expand((alpha>>(((x%4)*4+y%4)*4))&15,4),)
            else:
                # Interleave via a bit-string reference, independently of C shifts.
                index=sum(((x>>i)&1)*(4**i)+((y>>i)&1)*(2*4**i) for i in range(3))
                bit=(tile*64+index)*BITS[format];offset=bit//8
                b=data[offset:offset+max(1,BITS[format]//8)]
                word=int.from_bytes(b,'little')
                if format==0:rgba=(b[3],b[2],b[1],b[0])
                elif format==1:rgba=(b[2],b[1],b[0],255)
                elif format==2:rgba=(expand(word>>11,5),expand((word>>6)&31,5),expand((word>>1)&31,5),255 if word&1 else 0)
                elif format==3:rgba=(expand(word>>11,5),expand((word>>5)&63,6),expand(word&31,5),255)
                elif format==4:rgba=tuple(expand((word>>shift)&15,4) for shift in (12,8,4,0))
                elif format==5:rgba=(b[1],b[1],b[1],b[0])
                elif format==6:rgba=(b[1],b[0],0,255)
                elif format==7:rgba=(b[0],b[0],b[0],255)
                elif format==8:rgba=(0,0,0,b[0])
                elif format==9:rgba=(expand(b[0]>>4,4),)*3+(expand(b[0]&15,4),)
                else:
                    value=expand((b[0]>>(bit%8))&15,4)
                    rgba=(value,value,value,255) if format==10 else (0,0,0,value)
            result[(y*width+x)*4:(y*width+x+1)*4]=bytes(rgba)
    return bytes(result)
