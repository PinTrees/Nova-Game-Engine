#include "pch.h"
#include "Matrix3.h"


Matrix3::Matrix3()
{
    setDiagonal(1.0f);
}

Matrix3::Matrix3(float value)
{
    setDiagonal(value);
}

// 생성자: 행 우선 순서로 초기화
Matrix3::Matrix3(float v1, float v2, float v3)
{
    entries[0] = v1;
    entries[1] = 0.0f;
    entries[2] = 0.0f;

    entries[3] = 0.0f;
    entries[4] = v2;
    entries[5] = 0.0f;

    entries[6] = 0.0f;
    entries[7] = 0.0f;
    entries[8] = v3;
}

Matrix3::Matrix3(Quaternion q)
{
    float xx = q.x * q.x;
    float yy = q.y * q.y;
    float zz = q.z * q.z;
    float xy = q.x * q.y;
    float xz = q.x * q.z;
    float yz = q.y * q.z;
    float wx = q.w * q.x;
    float wy = q.w * q.y;
    float wz = q.w * q.z;

    // 첫 번째 행
    entries[0] = 1.0f - 2.0f * (yy + zz);
    entries[1] = 2.0f * (xy - wz);
    entries[2] = 2.0f * (xz + wy);

    // 두 번째 행
    entries[3] = 2.0f * (xy + wz);
    entries[4] = 1.0f - 2.0f * (xx + zz);
    entries[5] = 2.0f * (yz - wx);

    // 세 번째 행
    entries[6] = 2.0f * (xz - wy);
    entries[7] = 2.0f * (yz + wx);
    entries[8] = 1.0f - 2.0f * (xx + yy);
}

// 대각 행렬 설정: 행 우선 순서로 설정
void Matrix3::setDiagonal(float value)
{
    entries[0] = value;
    entries[1] = 0.0f;
    entries[2] = 0.0f;

    entries[3] = 0.0f;
    entries[4] = value;
    entries[5] = 0.0f;

    entries[6] = 0.0f;
    entries[7] = 0.0f;
    entries[8] = value;
}

void Matrix3::setDiagonal(Vec3 vec)
{
    entries[0] = vec.x; // 대각선의 첫 번째 성분 (x)
    entries[1] = 0.0f;
    entries[2] = 0.0f;

    entries[3] = 0.0f;
    entries[4] = vec.y; // 대각선의 두 번째 성분 (y)
    entries[5] = 0.0f;

    entries[6] = 0.0f;
    entries[7] = 0.0f;
    entries[8] = vec.z; // 대각선의 세 번째 성분 (z)
}

void Matrix3::setIdentity()
{
    entries[0] = 1.0f; // 첫 번째 행 첫 번째 열
    entries[1] = 0.0f;
    entries[2] = 0.0f;

    entries[3] = 0.0f;
    entries[4] = 1.0f; // 두 번째 행 두 번째 열
    entries[5] = 0.0f;

    entries[6] = 0.0f;
    entries[7] = 0.0f;
    entries[8] = 1.0f; // 세 번째 행 세 번째 열
}

Matrix3 Matrix3::transpose() const
{
    Matrix3 result(entries[0], entries[4], entries[8]);

    result.entries[1] = entries[3];
    result.entries[2] = entries[6];
    result.entries[3] = entries[1];
    result.entries[5] = entries[7];
    result.entries[6] = entries[2];
    result.entries[7] = entries[5];

    return result;
}

Matrix3 Matrix3::inverse() const
{
    float determinant =
        entries[0] * (entries[4] * entries[8] - entries[5] * entries[7])
        - entries[1] * (entries[3] * entries[8] - entries[5] * entries[6])
        + entries[2] * (entries[3] * entries[7] - entries[4] * entries[6]);

    /* 행렬식이 0 이면 역행렬이 존재하지 않는다 */
    if (determinant == 0.0f)
    {
        std::cout << "MATRIX3::This matrix's inverse does not exist." << std::endl;
        return *this;
    }

    determinant = 1.0f / determinant;
    Matrix3 result;

    result.entries[0] = (entries[4] * entries[8] - entries[5] * entries[7]) * determinant;
    result.entries[1] = -(entries[1] * entries[8] - entries[2] * entries[7]) * determinant;
    result.entries[2] = (entries[1] * entries[5] - entries[2] * entries[4]) * determinant;
    result.entries[3] = -(entries[3] * entries[8] - entries[5] * entries[6]) * determinant;
    result.entries[4] = (entries[0] * entries[8] - entries[2] * entries[6]) * determinant;
    result.entries[5] = -(entries[0] * entries[5] - entries[2] * entries[3]) * determinant;
    result.entries[6] = (entries[3] * entries[7] - entries[4] * entries[6]) * determinant;
    result.entries[7] = -(entries[0] * entries[7] - entries[1] * entries[6]) * determinant;
    result.entries[8] = (entries[0] * entries[4] - entries[1] * entries[3]) * determinant;

    return result;
}

Matrix3 Matrix3::operator+(const Matrix3& other) const
{
    Matrix3 result;

    for (int i = 0; i < 9; ++i)
    {
        result.entries[i] = entries[i] + other.entries[i];
    }

    return result;
}

void Matrix3::operator+=(const Matrix3& other)
{
    for (int i = 0; i < 9; ++i)
    {
        entries[i] += other.entries[i];
    }
}

Matrix3 Matrix3::operator-(const Matrix3& other) const
{
    Matrix3 result;

    for (int i = 0; i < 9; ++i)
    {
        result.entries[i] = entries[i] - other.entries[i];
    }

    return result;
}

void Matrix3::operator-=(const Matrix3& other)
{
    for (int i = 0; i < 9; ++i)
    {
        entries[i] -= other.entries[i];
    }
}

Matrix3 Matrix3::operator*(const Matrix3& other) const
{
    Matrix3 result;

    result.entries[0] = entries[0] * other.entries[0] +
        entries[1] * other.entries[3] + entries[2] * other.entries[6];
    result.entries[1] = entries[0] * other.entries[1] +
        entries[1] * other.entries[4] + entries[2] * other.entries[7];
    result.entries[2] = entries[0] * other.entries[2] +
        entries[1] * other.entries[5] + entries[2] * other.entries[8];

    result.entries[3] = entries[3] * other.entries[0] +
        entries[4] * other.entries[3] + entries[5] * other.entries[6];
    result.entries[4] = entries[3] * other.entries[1] +
        entries[4] * other.entries[4] + entries[5] * other.entries[7];
    result.entries[5] = entries[3] * other.entries[2] +
        entries[4] * other.entries[5] + entries[5] * other.entries[8];

    result.entries[6] = entries[6] * other.entries[0] +
        entries[7] * other.entries[3] + entries[8] * other.entries[6];
    result.entries[7] = entries[6] * other.entries[1] +
        entries[7] * other.entries[4] + entries[8] * other.entries[7];
    result.entries[8] = entries[6] * other.entries[2] +
        entries[7] * other.entries[5] + entries[8] * other.entries[8];

    return result;
}

void Matrix3::operator*=(const Matrix3& other)
{
    Matrix3 result;

    result.entries[0] = entries[0] * other.entries[0] +
        entries[1] * other.entries[3] + entries[2] * other.entries[6];
    result.entries[1] = entries[0] * other.entries[1] +
        entries[1] * other.entries[4] + entries[2] * other.entries[7];
    result.entries[2] = entries[0] * other.entries[2] +
        entries[1] * other.entries[5] + entries[2] * other.entries[8];

    result.entries[3] = entries[3] * other.entries[0] +
        entries[4] * other.entries[3] + entries[5] * other.entries[6];
    result.entries[4] = entries[3] * other.entries[1] +
        entries[4] * other.entries[4] + entries[5] * other.entries[7];
    result.entries[5] = entries[3] * other.entries[2] +
        entries[4] * other.entries[5] + entries[5] * other.entries[8];

    result.entries[6] = entries[6] * other.entries[0] +
        entries[7] * other.entries[3] + entries[8] * other.entries[6];
    result.entries[7] = entries[6] * other.entries[1] +
        entries[7] * other.entries[4] + entries[8] * other.entries[7];
    result.entries[8] = entries[6] * other.entries[2] +
        entries[7] * other.entries[5] + entries[8] * other.entries[8];

    *this = result;
}

SimpleMath::Vector3 Matrix3::operator*(SimpleMath::Vector3 vec) const 
{ 
    return SimpleMath::Vector3(
        entries[0] * vec.x + entries[1] * vec.y + entries[2] * vec.z, 
        entries[3] * vec.x + entries[4] * vec.y + entries[5] * vec.z,
        entries[6] * vec.x + entries[7] * vec.y + entries[8] * vec.z
    ); 
}

Matrix3 Matrix3::operator*(const float value) const
{
    Matrix3 result;

    for (int i = 0; i < 9; ++i)
    {
        result.entries[i] *= value;
    }

    return result;
}

void Matrix3::operator*=(const float value)
{
    for (int i = 0; i < 9; ++i)
    {
        entries[i] *= value;
    }
}

Matrix3& Matrix3::operator=(const Matrix3& other)
{
    for (int i = 0; i < 9; ++i)
        entries[i] = other.entries[i];
    return *this;
}
