#pragma once
// Windows 밖 (안드로이드) 에서 DirectXMath 를 쓰기 위한 SAL 주석 대체 — 모두 비운다 (분석용 표시일 뿐 코드에 영향 없음)
#ifndef _In_
#define _In_
#define _Out_
#define _Inout_
#define _In_opt_
#define _Out_opt_
#define _Inout_opt_
#define _In_reads_(n)
#define _In_reads_bytes_(n)
#define _Out_writes_(n)
#define _Out_writes_bytes_(n)
#define _Success_(e)
#define _Analysis_assume_(e)
#define _Use_decl_annotations_
#endif
