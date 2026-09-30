# 이펙트 컴포넌트 아이콘 (Particle System). make_icons.py 가 ICONS 에 합친다.

def _svg(body):
    return '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">\n  ' + body + '\n</svg>\n'


EFFECT_ICONS = {
    # Particle System: 아래 원뿔에서 위로 퍼지는 입자 (Unity 아이콘과 비슷한 모양)
    "particle_system": _svg('''<polygon points="6.2,14.5 9.8,14.5 12.5,8.5 3.5,8.5" fill="#4A7BA8"/>
  <circle cx="8" cy="6" r="1.7" fill="#F6D365"/>
  <circle cx="4.2" cy="4.2" r="1.3" fill="#F6B26B"/>
  <circle cx="11.8" cy="4" r="1.3" fill="#F6B26B"/>
  <circle cx="7" cy="2" r="1" fill="#FFE599"/>
  <circle cx="10.2" cy="1.6" r="0.8" fill="#FFE599"/>
  <circle cx="2.6" cy="7.2" r="0.8" fill="#FFE599"/>
  <circle cx="13.4" cy="7" r="0.8" fill="#FFE599"/>'''),
}
