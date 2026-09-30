# UI(Canvas) 컴포넌트 아이콘 (Inspector 헤더, Add Component, Object Picker). make_icons.py 가 ICONS 에 합친다.

def _svg(body):
    return '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">\n  ' + body + '\n</svg>\n'


UI_ICONS = {
    # Rect Transform: 사각형 + 네 모서리 점 (Unity 아이콘과 같은 모양)
    "rect_transform": _svg('''<rect x="3" y="3" width="10" height="10" fill="none" stroke="#C8C8C8" stroke-width="1.2"/>
  <rect x="1.5" y="1.5" width="3" height="3" fill="#6FA8DC"/>
  <rect x="11.5" y="1.5" width="3" height="3" fill="#6FA8DC"/>
  <rect x="1.5" y="11.5" width="3" height="3" fill="#6FA8DC"/>
  <rect x="11.5" y="11.5" width="3" height="3" fill="#6FA8DC"/>'''),
    # Canvas: 화면 틀 + 안쪽 UI 요소
    "canvas": _svg('''<rect x="1.5" y="2.5" width="13" height="11" rx="1" fill="#2E2E2E" stroke="#C8C8C8" stroke-width="1.2"/>
  <rect x="3.5" y="4.5" width="5" height="3" fill="#6FA8DC"/>
  <rect x="3.5" y="9" width="9" height="2.5" fill="#9FC5E8"/>'''),
    # Canvas Scaler: 틀 + 대각선 확대 화살표
    "canvas_scaler": _svg('''<rect x="1.5" y="2.5" width="13" height="11" rx="1" fill="none" stroke="#C8C8C8" stroke-width="1.2"/>
  <line x1="4.5" y1="11" x2="11.5" y2="5" stroke="#6FA8DC" stroke-width="1.4"/>
  <polygon points="12,4.5 8.8,4.8 11.7,7.7" fill="#6FA8DC"/>'''),
    # Graphic Raycaster: 마우스 포인터
    "graphic_raycaster": _svg('''<polygon points="4,2 4,13.5 7,10.8 9,14.5 10.8,13.6 8.8,10 12.5,10" fill="#E6E6E6" stroke="#303030" stroke-width="0.6"/>'''),
    # Event System: 두 개의 둥근 화살표 (입력 → UI)
    "event_system": _svg('''<circle cx="8" cy="8" r="5.5" fill="none" stroke="#C8C8C8" stroke-width="1.3"/>
  <polygon points="12.8,4.2 14.8,7.8 11,7.6" fill="#6FA8DC"/>
  <circle cx="8" cy="8" r="1.8" fill="#6FA8DC"/>'''),
    # Image: 그림 틀 + 산 + 해
    "ui_image": _svg('''<rect x="1.5" y="2.5" width="13" height="11" rx="1" fill="#3A5F85" stroke="#C8C8C8" stroke-width="1.1"/>
  <polygon points="2.5,12.5 6.5,7.5 9,10.3 10.8,8.6 13.5,12.5" fill="#9FC5E8"/>
  <circle cx="11" cy="5.5" r="1.4" fill="#F6D365"/>'''),
    # Texture (스프라이트 선택): 체크무늬 그림
    "texture": _svg('''<rect x="1.5" y="1.5" width="13" height="13" fill="#B8B8B8"/>
  <rect x="1.5" y="1.5" width="6.5" height="6.5" fill="#E6E6E6"/>
  <rect x="8" y="8" width="6.5" height="6.5" fill="#E6E6E6"/>
  <rect x="1.5" y="1.5" width="13" height="13" fill="none" stroke="#707070" stroke-width="0.8"/>'''),
    # Text: 굵은 T
    "ui_text": _svg('''<rect x="2.5" y="2.5" width="11" height="2.6" fill="#E6E6E6"/>
  <rect x="6.7" y="2.5" width="2.6" height="11" fill="#E6E6E6"/>'''),
    # Button: 둥근 버튼 + 손가락 누름 표시
    "ui_button": _svg('''<rect x="1.5" y="4" width="13" height="8" rx="2.5" fill="#D9D9D9" stroke="#8C8C8C" stroke-width="0.8"/>
  <rect x="4.5" y="7.3" width="7" height="1.6" fill="#555555"/>'''),
    # Font: A
    "font": _svg('''<polygon points="8,2 13.5,14 11.3,14 10.1,11.2 5.9,11.2 4.7,14 2.5,14" fill="#E6E6E6"/>
  <polygon points="8,6 9.4,9.4 6.6,9.4" fill="#383838"/>'''),
}
