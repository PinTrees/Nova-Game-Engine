#pragma once

struct Matrix3
{
    float entries[9];

    /* 생성자 */
    Matrix3();            // 단위행렬로 초기화
    Matrix3(float value); // 행렬의 대각성분 (diagonal entry) 을 주어진 값으로 초기화
    Matrix3(float v1, float v2, float v3);
    Matrix3(Quaternion q);

    /* 행렬의 대각성분을 주어진 값으로 설정한다
        나머지 성분은 0으로 설정한다 */
    void setDiagonal(float value);
    void setDiagonal(Vec3 vec);
    void setIdentity();

    /* 행렬의 전치행렬을 반환한다 */
    Matrix3 transpose() const;

    /* 행렬의 역행렬을 반환한다 */
    Matrix3 inverse() const;

    /*****************
     * 연산자 오버로딩 *
     *****************/

     /* 행렬끼리 더하기 */
    Matrix3 operator+(const Matrix3& other) const;
    void operator+=(const Matrix3& other);

    /* 행렬끼리 빼기 */
    Matrix3 operator-(const Matrix3& other) const;
    void operator-=(const Matrix3& other);

    /* 행렬끼리 곱하기 */
    Matrix3 operator*(const Matrix3& other) const;
    void operator*=(const Matrix3& other);

    /* 벡터와 곱하기 */
    Vector3 operator*(Vector3 vec) const;

    /* 실수와 곱하기 */
    Matrix3 operator*(const float value) const;
    void operator*=(const float value);

    Matrix3& operator=(const Matrix3& other);
};