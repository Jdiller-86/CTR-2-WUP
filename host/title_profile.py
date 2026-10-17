"""Independent checks for the original WUP Menu profile, not console acceptance.

Field/type facts: woominstaller_gx2 d2528e58, Flappy-Bird_GX2 43a541d9;
ACP layout: pinned WUT nn/acp/title.h. No upstream template or asset is used.
Historical integrity-only packages intentionally remain separately readable.
"""
import hashlib
import re
import struct
import xml.etree.ElementTree as ET

from .package_crypto import fail

ARTWORK = {
    'meta/iconTex.tga': (128,128,32),
    'meta/bootLogoTex.tga': (170,42,32),
    'meta/bootDrcTex.tga': (854,480,24),
    'meta/bootTvTex.tga': (1280,720,24),
}
BOOT_MOVIE_SHA256='366b8d210cae7ee92f746b2a1f55359a359e5ad5281a9c3086d60acd84739040'
LAUNCH_ASSETS=set(ARTWORK)|{'meta/bootMovie.h264'}


def _document(data, root):
    if (not data or len(data)>65536 or b'<!DOCTYPE' in data.upper()
            or b'<!ENTITY' in data.upper()):
        fail('invalid_menu_profile','Missing, oversized or unsafe title XML')
    try:
        doc=ET.fromstring(data)
    except ET.ParseError:
        fail('invalid_menu_profile','Invalid title XML')
    if (doc.tag!=root or doc.get('type')!='complex' or doc.get('access')!='777'
            or len({e.tag for e in doc})!=len(doc)):
        fail('invalid_menu_profile','Invalid title XML root or duplicate field')
    return doc


def _field(doc, name, kind, size):
    node=doc.find(name)
    if node is None or list(node) or node.get('type')!=kind or node.get('length')!=str(size):
        fail('invalid_menu_profile','Missing or incorrectly typed Menu/launch field: '+name)
    value=node.text or ''
    if (kind=='hexBinary' and not re.fullmatch('[0-9a-fA-F]{'+str(size*2)+'}',value)
            or kind=='unsignedInt' and (not re.fullmatch('[0-9]{1,10}',value) or int(value)>=2**32)
            or kind=='string' and (len(value.encode('utf-8'))>=size
                                   or any(ord(c)<32 for c in value))):
        fail('invalid_menu_profile','Out-of-range Menu/launch field: '+name)
    return value


def verify_menu_profile(files):
    """Validate launch XML, original TGA 2 artwork and the pinned original clip."""
    app=_document(files.get('code/app.xml',b''),'app')
    cos=_document(files.get('code/cos.xml',b''),'app')
    meta=_document(files.get('meta/meta.xml',b''),'menu')
    for name in ('version','sdk_version'):
        _field(app,name,'unsignedInt',4)
    for name,size in (('os_version',8),('title_id',8),('title_version',2),
                      ('app_type',4),('group_id',4),('os_mask',32),('common_id',8)):
        _field(app,name,'hexBinary',size)
    _field(cos,'version','unsignedInt',4)
    _field(cos,'cmdFlags','unsignedInt',4)
    entry=_field(cos,'argstr','string',4096)
    if not re.fullmatch(r'[A-Za-z0-9_-]+\.rpx',entry) or 'code/'+entry not in files:
        fail('invalid_menu_profile','COS must select a staged native RPX')
    for name in ('avail_size','codegen_size','codegen_core','max_size','max_codesize'):
        _field(cos,name,'hexBinary',4)
    if int(cos.findtext('codegen_core'),16) not in (1,2,4):
        fail('invalid_menu_profile','COS codegen core must select a platform core')
    for core in range(3):
        for prefix in ('default_stack','default_redzone','exception_stack'):
            _field(cos,f'{prefix}{core}_size','hexBinary',4)
    permissions=cos.find('permissions')
    if permissions is None or permissions.get('type')!='complex' or not 1<=len(permissions)<=19:
        fail('invalid_menu_profile','COS requires bounded platform permissions')
    groups=set()
    for i,node in enumerate(permissions):
        if node.tag!=f'p{i}' or node.get('type')!='complex':
            fail('invalid_menu_profile','Invalid COS permission entry')
        group=int(_field(node,'group','unsignedInt',4))
        _field(node,'mask','hexBinary',8)
        if group in groups:
            fail('invalid_menu_profile','Duplicate COS permission group')
        groups.add(group)
    if not {11,13,18}<=groups:
        fail('invalid_menu_profile','Original native app requires FS, MCP and ACP')
    for name,size in (('product_code',32),('content_platform',32),('company_code',8),
                      ('mastering_date',32),('ext_dev_etc_name',512)):
        _field(meta,name,'string',size)
    for name in ('version','logo_type','closing_msg','title_version','save_no_rollback',
                 'bg_daemon_enable','olv_accesskey','wood_tin','e_manual','e_manual_version',
                 'ext_dev_nunchaku','ext_dev_classic','ext_dev_urcc','ext_dev_board',
                 'ext_dev_usb_keyboard','ext_dev_etc','eula_version','drc_use','network_use',
                 'online_account_use','direct_boot'):
        _field(meta,name,'unsignedInt',4)
    for suffix in ('cero','esrb','bbfc','usk','pegi_gen','pegi_fin','pegi_prt',
                   'pegi_bbfc','cob','grb','cgsrr','oflc','reserved0','reserved1','reserved2','reserved3'):
        _field(meta,'pc_'+suffix,'unsignedInt',4)
    for name in ('app_launch_type','invisible_flag','no_managed_flag','no_event_log',
                 'no_icon_database','launching_flag','install_flag','group_id','region','join_game_id'):
        _field(meta,name,'hexBinary',4)
    for name in ('title_id','boss_id','os_version','app_size','common_save_size',
                 'account_save_size','common_boss_size','account_boss_size','join_game_mode_mask'):
        _field(meta,name,'hexBinary',8)
    for prefix,count in (('reserved_flag',8),('add_on_unique_id',32)):
        for i in range(count):
            _field(meta,f'{prefix}{i}','hexBinary',4)
    for language in ('ja','en','fr','de','it','es','zhs','ko','nl','pt','ru','zht'):
        for prefix,size in (('longname',512),('shortname',256),('publisher',256)):
            if not _field(meta,prefix+'_'+language,'string',size):
                fail('invalid_menu_profile','All Menu languages require original title information')
    if (meta.findtext('content_platform')!='WUP' or int(meta.findtext('app_launch_type'),16)!=0
            or any(int(meta.findtext(n),16) for n in ('invisible_flag','no_managed_flag','no_icon_database'))
            or meta.findtext('drc_use')!='1'):
        fail('invalid_menu_profile','Original title must be visible and launchable with GamePad support')
    for field in ('title_id','os_version','group_id'):
        if int(app.findtext(field),16)!=int(meta.findtext(field),16):
            fail('invalid_menu_profile','App/Menu identity mismatch: '+field)
    if int(app.findtext('title_version'),16)!=int(meta.findtext('title_version')):
        fail('invalid_menu_profile','App/Menu version mismatch')
    for name,(width,height,depth) in ARTWORK.items():
        data=files.get(name,b'')
        if len(data)!=18+width*height*(depth//8)+26:
            fail('invalid_menu_profile','Missing, truncated or oversized TGA 2 artwork: '+name)
        header=struct.unpack('<BBBHHBHHHHBB',data[:18])
        if (header!=(0,0,2,0,0,0,0,0,width,height,depth,8 if depth==32 else 0)
                or data[-26:]!=bytes(8)+b'TRUEVISION-XFILE.\0'):
            fail('invalid_menu_profile','Artwork must use original uncompressed bottom-left TGA 2: '+name)
    # ACP loads the movie separately from XML and the icon. A well-formed XML
    # profile cannot compensate for an absent/truncated launch resource. This
    # scoped original profile accepts only our independently decoded clip.
    movie=files.get('meta/bootMovie.h264',b'')
    if not 0<len(movie)<=80696 or hashlib.sha256(movie).hexdigest()!=BOOT_MOVIE_SHA256:
        fail('invalid_menu_profile','Require the complete original ACP launch movie')
    return 'wup-menu-v2'
