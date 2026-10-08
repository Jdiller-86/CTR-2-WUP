"""Original homebrew title staging. This output is not an installable WUP.

No donor title, upstream XML template, certificate or key is copied. Launch
settings remain candidates until an encrypted package is installed on console.
"""
import json
from pathlib import Path
import shutil
import struct
import tempfile
import xml.etree.ElementTree as ET
from .atomic import publish_directory
from .build import _validate_rpx,_sha256,MIN_BUILD_FREE
from .errors import HostError
from .package_validator import _title_id,ValidatorError


def _xml(root,fields):
    tree=ET.Element(root,{'type':'complex','access':'777'})
    for name,kind,length,value in fields:
        ET.SubElement(tree,name,{'type':kind,'length':str(length)}).text=str(value)
    ET.indent(tree)
    return ET.tostring(tree,encoding='utf-8',xml_declaration=True)+b'\n'


def _tga(width,height,alpha=False):
    """Original checker artwork; uncompressed, top-left origin, BGR(A)."""
    pixel=4 if alpha else 3
    header=struct.pack('<BBBHHBHHHHBB',0,0,2,0,0,0,0,0,width,height,pixel*8,0x28 if alpha else 0x20)
    row=[]
    for y in range(height):
        for x in range(width):
            rgb=(0x72,0x38,0x1a) if (x//32+y//32)%2 else (0xac,0x68,0x2a)
            row.append(bytes((*rgb,255)) if alpha else bytes(rgb))
    return header+b''.join(row)


def stage_title(rpx,output,*,title_id,name='CTR2WUP Prototype',version=0):
    try: title_id=_title_id(title_id)
    except ValidatorError as exc: raise HostError(exc.code,str(exc),stage='packaging') from exc
    if (not isinstance(name,str) or not name.strip() or len(name)>80
            or any(ord(c)<32 or 0xd800<=ord(c)<=0xdfff or ord(c) in (0xfffe,0xffff) for c in name)
            or len(name.encode('utf-8'))>255
            or not isinstance(version,int) or isinstance(version,bool) or not 0<=version<=65535):
        raise HostError('invalid_title_metadata','Use valid XML text of at most 80 characters/255 UTF-8 bytes and title version 0..65535',stage='packaging')
    rpx=Path(rpx).absolute()
    if rpx.is_symlink(): raise HostError('unsafe_file','RPX input must be a regular file',stage='packaging')
    _validate_rpx(rpx)
    output=Path(output).absolute()
    if output.exists():raise HostError('output_exists','Title staging output already exists',stage='prepare')
    output.parent.mkdir(parents=True,exist_ok=True)
    if shutil.disk_usage(output.parent).free<MIN_BUILD_FREE:
        raise HostError('insufficient_disk','Title staging requires 64 MiB free space',stage='prepare')
    stage=Path(tempfile.mkdtemp(prefix='.ctr2wup-title-',dir=output.parent))
    try:
        for folder in ('code','content','meta'):(stage/folder).mkdir()
        shutil.copyfile(rpx,stage/'code/ctr2wup.rpx')
        _validate_rpx(stage/'code/ctr2wup.rpx')
        if _sha256(rpx)!=_sha256(stage/'code/ctr2wup.rpx'):
            raise HostError('input_changed','RPX changed while staging',stage='packaging')
        app=[('version','unsignedInt',4,15),('os_version','hexBinary',8,'000500101000400a'),
             ('title_id','hexBinary',8,title_id),('title_version','hexBinary',2,f'{version:04x}'),
             ('sdk_version','unsignedInt',4,0),('app_type','hexBinary',4,'80000000'),
             ('group_id','hexBinary',2,'1337'),('os_mask','hexBinary',8,'0000000000000000')]
        cos=[('version','unsignedInt',4,18),('cmd_flags','hexBinary',4,'00000000'),
             ('argstr','string',64,'ctr2wup.rpx'),('avail_size','hexBinary',4,'00000000'),
             ('codegen_size','hexBinary',4,'00000000'),('codegen_core','hexBinary',4,'80000000'),
             ('max_size','hexBinary',4,'40000000'),('max_codesize','hexBinary',4,'03000000')]
        for core in range(3):
            cos += [(f'default_stack{core}_size','hexBinary',4,'00100000'),(f'default_redzone{core}_size','hexBinary',4,'00000000'),
                    (f'exception_stack{core}_size','hexBinary',4,'00001000')]
        meta=[('version','unsignedInt',4,33),('product_code','string',32,'WUP-N-CTRW'),
              ('company_code','string',8,'0000'),('mastering_date','string',32,''),
              ('logo_type','unsignedInt',4,0),('app_launch_type','hexBinary',4,'00000000'),
              ('title_id','hexBinary',8,title_id),('title_version','unsignedInt',4,version),
              ('region','hexBinary',4,'00000007')]
        for language in ('ja','en','fr','de','it','es','zhs','ko','nl','pt','ru','zht'):
            meta += [(f'longname_{language}','string',512,name),(f'shortname_{language}','string',256,name),
                     (f'publisher_{language}','string',256,'CTR2WUP contributors')]
        for relative,root,fields in [('code/app.xml','app',app),('code/cos.xml','app',cos),('meta/meta.xml','menu',meta)]:
            data=_xml(root,fields);ET.fromstring(data);(stage/relative).write_bytes(data)
        for filename,width,height,alpha in [('iconTex.tga',128,128,True),('bootTvTex.tga',1280,720,False),
                ('bootDrcTex.tga',854,480,False),('bootLogoTex.tga',170,42,True)]:
            (stage/'meta'/filename).write_bytes(_tga(width,height,alpha))
        report={'schema_version':1,'ok':True,'stage':'title-staging','title_id':title_id,'title_version':version,
                'title_name':name,'package_format':'code-content-meta staging','installable_wup':False,'console_verified':False,
                'identity_allocation':'explicit user ID; no console inventory/collision proof or registry reservation',
                'rpx_sha256':_sha256(stage/'code/ctr2wup.rpx'),
                'files':[{'path':str(p.relative_to(stage)).replace('\\','/'),'bytes':p.stat().st_size,'sha256':_sha256(p)}
                         for p in sorted(stage.rglob('*')) if p.is_file()],
                'remaining':['Confirm COS permissions/resources and metadata/artwork on console',
                             'Pack with the experimental encrypted writer and separately verify decrypted integrity',
                             'Configure user-local key/certificates and establish their provenance',
                             'Verify installer acceptance, Wii U Menu launch, rebuild identity and uninstall']}
        (stage/'stage-report.json').write_text(json.dumps(report,indent=2,ensure_ascii=True)+'\n')
        publish_directory(stage,output);return {**report,'output':str(output)}
    finally:
        if stage.exists():shutil.rmtree(stage)
