using NovaEngine;

namespace StarterAssets
{
    // 입력 값을 한곳에 모은다 (Unity Starter Assets 의 StarterAssetsInputs). 키보드 대신 다른 입력(UI 버튼, AI, 시험)을 쓰려면
    // readKeyboard 를 끄고 move / jump / sprint 를 직접 넣는다.
    public class StarterAssetsInputs : MonoBehaviour
    {
        [Header("Character Input Values")]
        public Vector2 move;
        public Vector2 look;      // 이번 프레임 마우스 이동 (오른쪽 버튼을 누른 동안, 픽셀)
        public bool jump;
        public bool sprint;

        [Header("Settings")]
        public bool readKeyboard = true;

        Vector3 lastMouse;
        bool dragging;

        void Update()
        {
            if (!readKeyboard) return;
            move = new Vector2(Input.GetAxisRaw("Horizontal"), Input.GetAxisRaw("Vertical"));
            sprint = Input.GetKey(KeyCode.LeftShift);
            if (Input.GetKeyDown(KeyCode.Space)) jump = true;
            // 카메라 돌리기: 오른쪽 버튼을 누른 채 끌기
            Vector3 m = Input.mousePosition;
            if (Input.GetMouseButton(1))
            {
                look = dragging ? new Vector2(m.x - lastMouse.x, m.y - lastMouse.y) : Vector2.zero;
                dragging = true;
            }
            else
            {
                look = Vector2.zero;
                dragging = false;
            }
            lastMouse = m;
        }
    }
}
