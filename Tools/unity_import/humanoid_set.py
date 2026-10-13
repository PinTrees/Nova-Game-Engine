"""휴머노이드 공용 애니메이션 세트 — 애니메이션 라이브러리에서 고른 클립을 프로젝트에 넣고
플레이어 · NPC 가 함께 쓰는 Animator Controller 와 C# 도우미를 만든다.

    python -I Tools/unity_import/humanoid_set.py --package E:\\NovaAssets\\AnimationLibrary --project E:\\NovaTest\\OpenWorld

만드는 것 (프로젝트 안):
  Assets/Animations/Humanoid/<묶음>/<상태 이름>.fbx      고른 클립 (상태 이름으로 바꿔 복사)
  Assets/Animations/Humanoid/HumanoidShared.controller   상태 머신 (아래)
  Assets/Animations/Humanoid/HumanoidShared.actions.json 상태 목록 (묶음 · 무기 · 원본)
  Assets/Scripts/Humanoid/HumanoidAnim.cs                무기 · 방향 열거형, 상태 이름 상수, 트리거 도우미

상태 머신 (기본 레이어 하나):
  Locomotion (Speed 1D: 서기 · 걷기 · 달리기 · 전력 질주) — ThirdPersonController 의 Speed · Grounded · Jump · FreeFall 그대로
  Loco_<무기> (Weapon 정수로 바뀜) · Crouch (Crouch) · Strafe (Strafe, MoveX · MoveY 2D)
  JumpStart → InAir → JumpLand
  <무기>_Attack1..4 (Attack 트리거 연타 = 연속기) · <무기>_Heavy (HeavyAttack) · <무기>_Block (Block) · <무기>_BlockHit (BlockHit)
  Any State: Hit / HitHard / Dodge (트리거) · Dead (불) — 방향은 Direction (0 앞 · 1 뒤 · 2 왼 · 3 오른)
  그 밖의 동작 (무기별 기술 · 넘어짐 · 기절 · 대화 · 줍기 · 넘기 · 오르기 · 수영 …) 은 연결 없이 두고
  스크립트가 HumanoidAnim.Play(animator, 이름) (= CrossFade) 로 튼다. 끝나면 Locomotion 으로 돌아온다.

컨트롤러는 불러올 때 클립을 모두 읽는다. 라이브러리 4760 개를 다 넣으면 수 GB · 수 분이라
종류마다 대표 클립을 골라 넣는다 (약 250 개). 더 필요한 클립은 EXTRA 표에 더하고 다시 실행한다.
"""
import argparse
import json
import math
import os
import re
import shutil
import sys

# ------------------------------------------------------------------ 고르는 표
# 값: 파일 이름 (확장자 없이, 대소문자 무시) 또는 '~정규식' (첫 일치, 제자리 클립 우선)

LOCO = {
    'Idle': 'Movement_St_Idle_00',
    'Walk': 'Movement_St_Walk_F_IPC',
    'Run': 'Movement_St_Run_F_IPC',
    'Sprint': 'Movement_Sprint_IPC',
    'CrouchIdle': 'Movement_Cr_Idle_00',
    'CrouchWalk': 'Movement_Cr_Walk_F_IPC',
    'JumpStart': 'Movement_St_Jump_Run_Start',
    'InAir': 'Movement_St_Jump_Run_Loop',
    'JumpLand': 'Movement_St_Jump_Run_End',
}

# Strafe 2D (MoveX, MoveY — 몸 기준 m/s): 걷기는 반지름 2, 달리기는 4
STRAFE = [
    ('Strafe_Idle', 'Movement_Act_Idle_00', 0, 0),
    ('Strafe_Walk_F', 'Movement_Act_Walk_F_IPC', 0, 2), ('Strafe_Walk_B', 'Movement_Act_Walk_B_IPC', 0, -2),
    ('Strafe_Walk_L', 'Movement_Act_Walk_L_IPC', -2, 0), ('Strafe_Walk_R', 'Movement_Act_Walk_R_IPC', 2, 0),
    ('Strafe_Walk_FL', 'Movement_Act_Walk_45L_IPC', -1.414, 1.414), ('Strafe_Walk_FR', 'Movement_Act_Walk_45R_IPC', 1.414, 1.414),
    ('Strafe_Walk_BL', 'Movement_Act_Walk_135L_IPC', -1.414, -1.414), ('Strafe_Walk_BR', 'Movement_Act_Walk_135R_IPC', 1.414, -1.414),
    ('Strafe_Run_F', 'Movement_Act_Run_F_IPC', 0, 4), ('Strafe_Run_B', 'Movement_Act_Run_B_IPC', 0, -4),
    ('Strafe_Run_L', 'Movement_Act_Run_90L_IPC', -4, 0), ('Strafe_Run_R', 'Movement_Act_Run_90R_IPC', 4, 0),
    ('Strafe_Run_FL', 'Movement_Act_Run_45L_IPC', -2.828, 2.828), ('Strafe_Run_FR', 'Movement_Act_Run_45R_IPC', 2.828, 2.828),
    ('Strafe_Run_BL', 'Movement_Act_Run_135L_IPC', -2.828, -2.828), ('Strafe_Run_BR', 'Movement_Act_Run_135R_IPC', 2.828, -2.828),
]

# 공통 반응 (Any State, Direction 순서 F B L R)
DIRS = ['F', 'B', 'L', 'R']
REACT = {
    'Hit': ['Movement_St_Hit_Light_F', 'Movement_St_Hit_Light_B', 'Movement_St_Hit_Light_L', 'Movement_St_Hit_Light_R'],
    'HitHard': ['Movement_St_Hit_Hard_F', 'Movement_St_Hit_Hard_B', 'Movement_St_Hit_Hard_L', 'Movement_St_Hit_Hard_R'],
    'Dodge': ['Movement_Act_Dodge_Shift_F', 'Movement_Act_Dodge_B', 'Movement_Act_Dodge_L', 'Movement_Act_Dodge_R'],
    'Death': ['Movement_St_Death_F', 'Movement_St_Death_B', 'Movement_St_Death_L', 'Movement_St_Death_R'],
}

# 무기 (Weapon 정수 = 순서 + 1). combo 는 Attack 연타, chain=True 면 한 번 누르면 끝까지 이어진다 (활)
WEAPONS = [
    {'key': 'SwordShield',
     'loco': ['SwordShield_Sword_St_Idle01', 'SwordShield_Sword_St_Walk_F', 'SwordShield_Sword_St_Run_F', None],
     'combo': ['SwordShield_Sword_Melee_Sword01', 'SwordShield_Sword_Melee_Sword02', 'SwordShield_Sword_Melee_Sword03', 'SwordShield_Sword_Melee_Sword04'],
     'heavy': 'SwordShield_Sword_Melee_Shield01', 'block': 'SwordShield_Sword_Block_Shield_F', 'blockHit': 'SwordShield_Sword_Block_Sword_F',
     'extra': {'Equip': 'SwordShield_Sword_St_TakeUp', 'Unequip': 'SwordShield_Sword_St_HideUp',
               'Slash5': 'SwordShield_Sword_Melee_Sword05', 'Slash6': 'SwordShield_Sword_Melee_Sword06', 'Kick': 'SwordShield_Sword_Melee_Kick',
               'ShieldBash2': 'SwordShield_Sword_Melee_Shield02', 'ShieldBash3': 'SwordShield_Sword_Melee_Shield03',
               'BlockLeft': 'SwordShield_Sword_Block_Shield_L01', 'BlockRight': 'SwordShield_Sword_Block_Shield_R',
               'Hit_F': 'SwordShield_Sword_St_Hit_Light_F', 'Hit_B': 'SwordShield_Sword_St_Hit_Light_B',
               'Idle2': 'SwordShield_Sword_St_Idle03', 'CrouchIdle': 'SwordShield_Sword_Cr_Idle01'}},
    {'key': 'Greatsword',
     'loco': ['GhostSamurai_GreatSword_Idle_Inplace', 'GhostSamurai_GreatSword_Strafe_Walk_F_Inplace', 'GhostSamurai_GreatSword_Strafe_Run_F_Inplace', 'GhostSamurai_GreatSword_Run_Sprint_Inplace'],
     'combo': ['GhostSamurai_GreatSword_Attack01_Inplace', 'GhostSamurai_GreatSword_Attack02_Inplace', 'GhostSamurai_GreatSword_Attack03_Inplace', 'GhostSamurai_GreatSword_Attack04_Inplace'],
     'heavy': 'GhostSamurai_GreatSword_SPAttack1_Inplace', 'block': 'GhostSamurai_GreatSword_Defense_Loop_Inplace', 'blockHit': 'GhostSamurai_GreatSword_Defense_Broken_Inplace',
     'extra': {'Equip': 'GhostSamurai_GreatSword_Equip_Inplace', 'Unequip': 'GhostSamurai_GreatSword_Unarm_Inplace',
               'Skill5': 'GhostSamurai_GreatSword_Attack05_Inplace', 'Skill6': 'GhostSamurai_GreatSword_Attack06_Inplace',
               'Skill7': 'GhostSamurai_GreatSword_Attack07_Inplace', 'AirAttack': 'GhostSamurai_GreatSword_Air_Attack01_Inplace',
               'Charge': 'GhostSamurai_GreatSword_FocusEnergy_R_Attack01_Inplace', 'Special2': 'GhostSamurai_GreatSword_SPAttack2_Inplace',
               'Execution': 'GhostSamurai_GreatSword_Attack_Execution1_Inplace', 'Executed': 'GhostSamurai_GreatSword_Attack_Executed1_Inplace',
               'Grruzam_Idle': 'Grruzam_M_Big_Sword_Idle'}},
    {'key': 'Katana',
     'loco': ['GhostSamurai_APose_Idle', 'GhostSamurai_APose_Strafe_Walk_F_Loop_Inplace', 'GhostSamurai_APose_Strafe_Run_F_Loop_Inplace', 'GhostSamurai_APose_Strafe_Run_F_Sprint_Inplace'],
     'combo': ['GhostSamurai_APose_Attack01_1_ALL_Inplace', 'GhostSamurai_APose_Attack01_2_Inplace', 'GhostSamurai_APose_Attack01_3_Inplace', 'GhostSamurai_APose_Attack01_4_Inplace'],
     'heavy': 'GhostSamurai_APose_SPAttack01_Inplace', 'block': 'GhostSamurai_DefenseL_Loop_Inplace', 'blockHit': 'GhostSamurai_DefenseL_Hit01_Inplace',
     'extra': {'Equip': 'GhostSamurai_APose_Equip01_Inplace', 'Unequip': 'GhostSamurai_APose_Unarm_1_Inplace',
               'Skill4': 'GhostSamurai_APose_Attack04_Inplace', 'Skill5': 'GhostSamurai_APose_Attack05_Inplace',
               'Skill6': 'GhostSamurai_APose_Attack06_Inplace', 'Skill7': 'GhostSamurai_APose_Attack07_Inplace',
               'Special2': 'GhostSamurai_APose_SPAttack02_Inplace', 'Special3': 'GhostSamurai_APose_SPAttack03_Inplace',
               'AirAttack': 'GhostSamurai_APose_Air_Attack02_Inplace', 'Parry': 'GhostSamurai_DefenseL_Parry01_Inplace', 'Deflect': 'GhostSamurai_LAttack_DeflectL90_Inplace', 'CounterExecution': 'GhostSamurai_LAttack_DeflectL_CounterExecution_Inplace',
               'Grruzam_Idle': 'Grruzam_M_katana_Blade_Idle_ver_A'}},
    {'key': 'SpearShield',
     'loco': ['SpearShield_Idle_Attacker', 'SpearShield_move_walk_front_inplace', 'SpearShield_move_run_front_inplace', None],
     'combo': ['SpearShield_Attack01', 'SpearShield_Attack02', 'SpearShield_Attack03', 'SpearShield_Attack04'],
     'heavy': 'SpearShield_Attack_Swing', 'block': '~SpearShield_Defense_loop', 'blockHit': '~SpearShield_Defense_breaked',
     'extra': {'Equip': '~SpearShield_.*equip', 'Unequip': '~SpearShield_.*unequip',
               'Kick': 'SpearShield_Attack05_Kick', 'ShieldBash': 'SpearShield_Attack_Shield01', 'StingDown': 'SpearShield_Attack_StingDown',
               'Throw': 'SpearShield_Attack_ThrowSpear', 'ComboStings': 'SpearShield_ComboStings01', 'DashSting': '~SpearShield_Attack_DashSting',
               'Roll_F': '~SpearShield_roll_front', 'Roll_B': '~SpearShield_roll_back', 'Roll_L': '~SpearShield_roll_left', 'Roll_R': '~SpearShield_roll_right',
               'Buff': '~SpearShield_Buff01', 'Dead': '~SpearShield_dead01'}},
    {'key': 'Staff',
     'loco': ['BoStaff_idle_inplace', 'BoStaff_move_C_walk_front_inplace', 'BoStaff_move_C_run_front_inplace', None],
     'combo': ['BoStaff_combo01_1_inplace', 'BoStaff_combo01_2_inplace', 'BoStaff_combo01_3_inplace', 'BoStaff_attack01_inplace'],
     'heavy': 'BoStaff_attack03_inplace', 'block': '~BoStaff_defense_loop', 'blockHit': '~BoStaff_defense(_inplace)?$',
     'extra': {'Equip': '~BoStaff_wp_equip', 'Unequip': '~BoStaff_wp_unequip',
               'Skill2': 'BoStaff_attack02_inplace', 'Skill4': 'BoStaff_attack04_inplace', 'Skill5': 'BoStaff_attack05_inplace',
               'Combo2': '~BoStaff_combo02(_inplace)?$', 'Avoid_F': 'BoStaff_avoid_front_inplace', 'Avoid_B': 'BoStaff_avoid_back_inplace',
               'Avoid_L': 'BoStaff_avoid_left_inplace', 'Avoid_R': 'BoStaff_avoid_right_inplace', 'Buff': 'BoStaff_buff01_inplace',
               'Down': '~BoStaff_down01', 'Rise': '~BoStaff_rise01'}},
    {'key': 'Bow', 'chain': True,
     'loco': ['GhostSamurai_Bow_Idle_Inplace', 'GhostSamurai_Bow_Walk_F_Inplace', 'GhostSamurai_Bow_Run_F_Inplace', 'GhostSamurai_Bow_Sprint_Inplace'],
     'combo': ['GhostSamurai_Bow_Shoot_Start_Inplace', 'GhostSamurai_Bow_Shoot_Loop_Inplace', 'GhostSamurai_Bow_Shoot_End_Inplace'],
     'heavy': 'GhostSamurai_Bow_Shoot_SP01_Inplace', 'block': None, 'blockHit': None,
     'extra': {'Equip': 'GhostSamurai_Bow_Common_Equip_Inplace', 'Unequip': 'GhostSamurai_Bow_Common_Unarm_Inplace',
               'AimIdle': 'GhostSamurai_Bow_Shoot_Aim_Idle_Inplace', 'Cancel': 'GhostSamurai_Bow_Shoot_Cancel_Inplace',
               'Special2': 'GhostSamurai_Bow_Shoot_SP02_Inplace', 'Special3': 'GhostSamurai_Bow_Shoot_SP03_Inplace',
               'RainStart': 'GhostSamurai_Bow_RainShoot_Start_Inplace', 'RainLoop': 'GhostSamurai_Bow_RainShoot_Loop_Inplace',
               'RainEnd': 'GhostSamurai_Bow_RainShoot_End_Inplace', 'AirShoot': 'GhostSamurai_Bow_AirShoot_Start_Inplace',
               'CrouchAim': 'GhostSamurai_Bow_Shoot_AimCrouch_Idle_Inplace'}},
    {'key': 'Unarmed',
     'loco': ['HandToHand_H2H_Idle', 'HandToHand_H2H_WalkForward', 'HandToHand_H2H_RunForward', 'HandToHand_H2H_SprintForward'],
     'combo': ['HandToHand_H2H_JabInPlace', 'HandToHand_H2H_HookPunch_InPlace', 'HandToHand_H2H_LeadingUppercut_InPlace', 'HandToHand_H2H_RoundhouseKick_InPlace'],
     'heavy': 'HandToHand_H2H_Haymaker_InPlace', 'block': 'HandToHand_H2H_BlockLoop', 'blockHit': 'HandToHand_H2H_HitReaction01',
     'extra': {'Equip': 'HandToHand_H2H_Equip', 'AxeKick': 'HandToHand_H2H_AxeKick_InPlace', 'HighKick': 'HandToHand_H2H_HighSideKick_InPlace',
               'LowKick': 'HandToHand_H2H_LowSideKick_InPlace', 'SpinKick': 'HandToHand_H2H_SpinningHookKick_InPlace',
               'LegSweep': 'HandToHand_H2H_LegSweep_InPlace', 'PowerPunch': 'HandToHand_H2H_PowerPunch01_Forward',
               'Backfist': 'HandToHand_H2H_SnappingBackfist_InPlace', 'SlipLeft': 'HandToHand_H2H_SlipLeft', 'SlipRight': 'HandToHand_H2H_SlipRight',
               'KnockDown': '~HandToHand_H2H_KnockDown01'}},
]

# 그 밖의 동작 (스크립트가 이름으로 튼다): 묶음 → {상태 이름: 클립}
EXTRA = {
    'Idle': {'Idle_LookAround': 'Social_MCU_am_Stand_Idle_03_LookAround', 'Idle_Shift1': 'Movement_St_Idle_01', 'Idle_Shift2': 'Movement_St_Idle_02',
             'Idle_Shift3': 'Movement_St_Idle_03', 'Idle_Shift4': 'Movement_St_Idle_04', 'Idle_Shift5': 'Movement_St_Idle_05',
             'Idle_Combat': 'Movement_Act_Idle_00', 'Idle_Victim': '~SpearShield_idle_victim'},
    'Reaction': {'Stun_Start': 'Movement_Act_Stun_Start', 'Stun_Loop': 'Movement_Act_Stun_Loop', 'Stun_End': 'Movement_Act_Stun_End',
                 'Knockdown_F_Start': '~GhostSamurai_APose_Falldown_F_Start', 'Knockdown_F_Loop': 'GhostSamurai_APose_Falldown_F_Loop_Inplace',
                 'Knockdown_F_End': '~GhostSamurai_APose_Falldown_F_End', 'GetUp': '~GhostSamurai_APose_Hit_Up_GetUP',
                 'Knockdown_B_Start': '~GhostSamurai_GreatSword_Falldown_B_Start', 'Knockdown_B_Loop': 'GhostSamurai_GreatSword_Falldown_B_Loop_Inplace',
                 'Knockdown_Front_StandUp': 'Grruzam_M_katana_Blade_Damage_Front_Down_StandUp',
                 'Knockdown_Back_StandUp': 'Grruzam_M_katana_Blade_Damage_Back_Down_StandUp',
                 'Knockdown_Roll_StandUp': 'Grruzam_M_katana_Blade_KnockDown_Front_FW_Rolling_StandUp',
                 'Knockdown_Smash': 'Grruzam_M_katana_Blade_KnockDown_Back_Smash'},
    'Social': {'Talk1': 'Social_MCU_am_StandConvA_Talk_01_Neutral', 'Talk2': 'Social_MCU_am_StandConvA_Talk_05_Positive',
               'Talk3': 'Social_MCU_am_StandConvA_Talk_06_Negative', 'Talk4': 'Social_MCU_am_StandConvA_Talk_03_Neutral',
               'Listen1': 'Social_MCU_am_StandConvA_Listen_02_Positive', 'Listen2': 'Social_MCU_am_StandConvA_Listen_03_Negative',
               'TalkF1': 'Social_MCU_af_StandConvB_Talk_01_Neutral', 'TalkF2': 'Social_MCU_af_StandConvB_Talk_03_Positive',
               'ListenF1': 'Social_MCU_af_StandConvB_Listen_01_Neutral', 'IdleF': 'Social_MCU_af_StandConvB_Idle_01',
               'ShakeHands': 'Social_MCU_am_StandConv_Greet_01_ShakeHands_PairA', 'Give': 'Social_MCU_am_StandConv_Trade_03_AGive_BTake_PairA',
               'Take': 'Social_MCU_am_StandConv_Trade_01_ATake_BGive_PairA', 'Reject': 'Social_MCU_am_StandConv_Trade_06_AReject_BGive_PairA',
               'TalkWalk': 'Social_MCU_am_StandConv_TalkWalk_01_PairA'},
    'Interaction': {'PickUp_Low': '~Movement_(St|Act)_PickUp_Low', 'PickUp_Mid': '~Movement_(St|Act)_PickUp_Mid', 'Throw': '~Movement_St_Grenade'},
    'Traversal': {'Vault_Low': '~Traversal_Fence_91cm_JumpOver_FromRunToRun', 'Vault_High': '~Traversal_Fence_130cm_JumpOver_FromRunToRun',
                  'Vault_Lazy': '~Traversal_Movement_LazyVault_Left', 'ClimbOver_Wall': '~Traversal_Fence_345cm_ClimbOver_Normal',
                  'Ledge_HangIdle': '~Traversal_Ledge_Climb_HangingIdle$', 'Ledge_ClimbUp': '~Traversal_Wall_Climb_End_toStanding',
                  'Ledge_ShimmyLeft': '~Traversal_LedgeWalk_Left_Loop$', 'Ladder_Idle': '~Traversal_Ladder_Climb_Idle',
                  'Ladder_Up': '~Traversal_Ladder_Climb_Up_Loop', 'Ladder_Down': '~Traversal_Ladder_Climb_Down_Loop',
                  'JumpDown': '~Traversal_Hole_JumpDown_WholeSequence', 'Slide': '~Traversal_Movement_Run_n_Slide_WholeSequence',
                  'FrontFlip': '~Traversal_Movement_FrontFlipJump_fromRuntoRun', 'BalanceBeam': '~Traversal_BalanceBeam_Forward_LoopFast'},
    'Swim': {'Swim_Idle': '~Swim_IP_.*(tread|idle_float_front|idle).*', 'Swim_Breaststroke': 'Swim_IP_Breastroke_overwater_01',
             'Swim_Crawl': 'Swim_IP_Crawl_01', 'Swim_Dash': 'Swim_IP_breastroke_dash_01', 'Swim_DiveStart': 'Swim_IP_dive_start_01',
             'Swim_DiveDown': 'Swim_IP_dive_loopDown', 'Swim_ClimbOut': 'Swim_IP_Climbing_edge_01', 'Swim_Death': 'Swim_IP_death_surface_01'},
}

# 이 이름이 들어 있으면 반복 동작 (끝나도 Locomotion 으로 돌아가지 않는다)
LOOP_HINT = re.compile(r'(Loop|Idle|Talk|Listen|Swim_(Idle|Breaststroke|Crawl|DiveDown)|Ladder_(Idle|Up|Down)|Ledge_(HangIdle|Shimmy)|BalanceBeam|AimIdle|CrouchAim)', re.I)

WEAPON_NAMES = ['None'] + [w['key'] for w in WEAPONS]


# ------------------------------------------------------------------ 라이브러리 찾기
class Library:
    def __init__(self, package):
        self.package = package
        with open(os.path.join(package, 'catalog.json'), encoding='utf-8') as f:
            self.catalog = json.load(f)
        self.by_stem = {}
        for e in self.catalog:
            stem = os.path.splitext(e['file'].split('/')[-1])[0]
            self.by_stem.setdefault(stem.lower(), []).append(e)
        self.missing = []

    def find(self, spec):
        if not spec:
            return None
        if spec.startswith('~'):
            rx = re.compile(spec[1:], re.I)
            hits = [e for e in self.catalog if rx.search(os.path.splitext(e['file'].split('/')[-1])[0])]
            # 제자리 → 짧은 이름 → 이름 순
            hits.sort(key=lambda e: (not e['inPlace'], 'root' in e['file'].lower(), len(e['file']), e['file']))
            if hits:
                return hits[0]
        else:
            hits = self.by_stem.get(spec.lower())
            if hits:
                return hits[0]
        self.missing.append(spec)
        return None


def frames(e):
    if e and e.get('clips'):
        c = e['clips'][0]
        return max(1.0, c['lastFrame'] - c['firstFrame'])
    return 0.0


# ------------------------------------------------------------------ 만들기
class Builder:
    def __init__(self, lib, project, dest, name):
        self.lib, self.project, self.dest, self.name = lib, project, dest.strip('/\\').replace('\\', '/'), name
        self.states, self.transitions, self.actions = [], [], []
        self.files = {}   # 프로젝트 상대 경로 → 라이브러리 파일
        self.names = set()

    def clip(self, group, state, spec):
        """클립을 묶음 폴더에 상태 이름으로 넣고 프로젝트 경로를 돌려준다"""
        e = self.lib.find(spec)
        if e is None:
            return None, None
        rel = '%s/%s/%s.fbx' % (self.dest, group, state)
        self.files[rel] = e['file']
        return rel.replace('/', '\\'), e

    def state(self, name, group, spec, loop, pos, weapon='', exit_to=None, exit_time=0.9, kind='action'):
        path, e = self.clip(group, name, spec)
        if path is None:
            return False
        assert name not in self.names, name
        self.names.add(name)
        self.states.append({'name': name, 'clipPath': path, 'clipIndex': 0, 'speed': 1.0, 'cycleOffset': 0.0,
                            'loop': bool(loop), 'writeDefaults': True, 'footIK': kind == 'loco', 'pos': list(pos)})
        self.actions.append({'state': name, 'group': group, 'weapon': weapon, 'kind': kind, 'loop': bool(loop),
                             'frames': frames(e), 'source': e['file']})
        if exit_to:
            self.trans(name, exit_to, exit_time=exit_time, duration=0.2)
        return True

    def tree(self, name, group, children, param, pos, kind='loco', weapon='', param_y=None):
        kids = []
        for i, (cname, spec, a, b) in enumerate(children):
            path, e = self.clip(group, cname, spec)
            if path is None:
                continue
            k = {'clipPath': path, 'clipIndex': 0, 'threshold': float(a), 'timeScale': 1.0}
            if param_y:
                k['pos'] = [float(a), float(b)]
            kids.append(k)
            self.actions.append({'state': name + '/' + cname, 'group': group, 'weapon': weapon, 'kind': 'blend', 'loop': True,
                                 'frames': frames(e), 'source': e['file']})
        if not kids:
            return False
        self.names.add(name)
        bt = {'type': 1 if param_y else 0, 'parameter': param, 'children': kids}
        if param_y:
            bt['parameterY'] = param_y
        self.states.append({'name': name, 'clipPath': '', 'clipIndex': 0, 'speed': 1.0, 'cycleOffset': 0.0, 'loop': True,
                            'writeDefaults': True, 'footIK': True, 'pos': list(pos), 'blendTree': bt})
        return True

    def trans(self, a, b, conds=(), exit_time=None, duration=0.25, to_self=True):
        self.transitions.append({'from': a, 'to': b, 'hasExitTime': exit_time is not None, 'exitTime': exit_time if exit_time is not None else 0.75,
                                 'fixedDuration': True, 'duration': duration, 'offset': 0.0, 'canTransitionToSelf': to_self,
                                 'mute': False, 'solo': False,
                                 'conditions': [{'parameter': p, 'mode': m, 'threshold': float(t)} for p, m, t in conds]})

    def build(self):
        ANY = '<Any State>'
        # 이동
        self.tree('Locomotion', 'Locomotion', [('Idle', LOCO['Idle'], 0, 0), ('Walk', LOCO['Walk'], 2.0, 0),
                                                ('Run', LOCO['Run'], 3.8, 0), ('Sprint', LOCO['Sprint'], 5.335, 0)], 'Speed', (0, 0))
        self.tree('Crouch', 'Locomotion', [('CrouchIdle', LOCO['CrouchIdle'], 0, 0), ('CrouchWalk', LOCO['CrouchWalk'], 1.5, 0)], 'Speed', (0, 140))
        self.tree('Strafe', 'Locomotion', STRAFE, 'MoveX', (0, -140), param_y='MoveY')
        family = ['Locomotion', 'Crouch', 'Strafe']
        self.trans('Locomotion', 'Crouch', [('Crouch', 'If', 0)])
        self.trans('Crouch', 'Locomotion', [('Crouch', 'IfNot', 0)])
        self.trans('Strafe', 'Locomotion', [('Strafe', 'IfNot', 0)])
        self.trans('Locomotion', 'Strafe', [('Strafe', 'If', 0)])

        # 무기별 이동 · 연속기 · 막기
        for wi, w in enumerate(WEAPONS, start=1):
            k = w['key']
            y = 280 + wi * 120
            idle, walk, run, sprint = w['loco']
            kids = [(k + '_Idle', idle, 0, 0), (k + '_Walk', walk, 2.0, 0), (k + '_Run', run, 3.8, 0)]
            if sprint:
                kids.append((k + '_Sprint', sprint, 5.335, 0))
            loco = 'Loco_' + k
            if not self.tree(loco, 'Weapons/' + k, kids, 'Speed', (-420, y), weapon=k):
                continue
            family.append(loco)
            self.trans('Locomotion', loco, [('Weapon', 'Equals', wi)])
            self.trans(loco, 'Locomotion', [('Weapon', 'NotEqual', wi)])
            self.trans(loco, 'Strafe', [('Strafe', 'If', 0)])
            starts = [loco] + (['Locomotion'] if k == 'Unarmed' else [])
            prev = None
            for ci, spec in enumerate(w['combo'], start=1):
                s = '%s_Attack%d' % (k, ci)
                if not self.state(s, 'Weapons/' + k, spec, False, (-150 + ci * 170, y), weapon=k):
                    break
                if prev is None:
                    for st in starts:
                        self.trans(st, s, [('Attack', 'If', 0)], duration=0.1)
                elif w.get('chain'):
                    self.trans(prev, s, exit_time=0.95, duration=0.08)
                else:
                    # 앞 공격 절반이 지나면 다음 공격 (미리 누른 트리거는 남아 있다)
                    self.trans(prev, s, [('Attack', 'If', 0)], exit_time=0.55, duration=0.12)
                prev = s
                last_chain = s
            # 공격이 끝나면 무기 이동으로
            for ci in range(1, len(w['combo']) + 1):
                s = '%s_Attack%d' % (k, ci)
                if s in self.names and (not w.get('chain') or s == last_chain):
                    self.trans(s, loco, exit_time=0.9, duration=0.25)
            if self.state(k + '_Heavy', 'Weapons/' + k, w['heavy'], False, (-150 + 5 * 170, y), weapon=k, exit_to=loco):
                for st in starts:
                    self.trans(st, k + '_Heavy', [('HeavyAttack', 'If', 0)], duration=0.1)
            if w['block'] and self.state(k + '_Block', 'Weapons/' + k, w['block'], True, (-150 + 6 * 170, y), weapon=k):
                self.trans(loco, k + '_Block', [('Block', 'If', 0)], duration=0.12)
                self.trans(k + '_Block', loco, [('Block', 'IfNot', 0)], duration=0.2)
                if w['blockHit'] and self.state(k + '_BlockHit', 'Weapons/' + k, w['blockHit'], False, (-150 + 7 * 170, y), weapon=k):
                    self.trans(k + '_Block', k + '_BlockHit', [('BlockHit', 'If', 0)], duration=0.05)
                    self.trans(k + '_BlockHit', k + '_Block', exit_time=0.85, duration=0.15)
            for xi, (xn, spec) in enumerate(w['extra'].items()):
                loop = bool(LOOP_HINT.search(xn))
                self.state('%s_%s' % (k, xn), 'Weapons/' + k, spec, loop, (-150 + (8 + xi) * 170, y), weapon=k,
                           exit_to=None if loop else loco)

        # 점프 · 낙하
        self.state('JumpStart', 'Locomotion', LOCO['JumpStart'], False, (420, -60), kind='loco')
        self.state('InAir', 'Locomotion', LOCO['InAir'], True, (640, -60), kind='loco')
        self.state('JumpLand', 'Locomotion', LOCO['JumpLand'], False, (860, -60), kind='loco')
        for st in family:
            self.trans(st, 'JumpStart', [('Jump', 'If', 0)], duration=0.1)
            self.trans(st, 'InAir', [('FreeFall', 'If', 0)], duration=0.2)
        self.trans('JumpStart', 'InAir', exit_time=0.9, duration=0.1)
        self.trans('JumpStart', 'JumpLand', [('Grounded', 'If', 0), ('Jump', 'IfNot', 0)], exit_time=0.5, duration=0.1)
        self.trans('InAir', 'JumpLand', [('Grounded', 'If', 0)], duration=0.1)
        self.trans('JumpLand', 'Locomotion', [('Speed', 'Greater', 1.0)], exit_time=0.3, duration=0.15)
        self.trans('JumpLand', 'Locomotion', exit_time=0.75, duration=0.2)

        # Any State 반응 (방향별)
        for kind, specs in REACT.items():
            for di, spec in enumerate(specs):
                s = '%s_%s' % (kind, DIRS[di])
                loop = False
                if kind == 'Death':
                    ok = self.state(s, 'Reaction', spec, False, (420 + di * 170, 160), kind='reaction')
                    if ok:
                        self.trans(ANY, s, [('Dead', 'If', 0), ('Direction', 'Equals', di)], duration=0.15, to_self=False)
                        self.trans(s, 'Locomotion', [('Dead', 'IfNot', 0)], duration=0.4)
                    continue
                ok = self.state(s, 'Reaction', spec, loop, (420 + di * 170, {'Hit': 260, 'HitHard': 330, 'Dodge': 400}[kind]),
                                kind='reaction', exit_to='Locomotion', exit_time=0.85)
                if ok:
                    self.trans(ANY, s, [(kind, 'If', 0), ('Direction', 'Equals', di), ('Dead', 'IfNot', 0)], duration=0.08, to_self=True)

        # 그 밖의 동작
        for gi, (group, items) in enumerate(EXTRA.items()):
            for xi, (xn, spec) in enumerate(items.items()):
                loop = bool(LOOP_HINT.search(xn))
                self.state(xn, group, spec, loop, (1200 + xi * 170, -300 + gi * 90), exit_to=None if loop else 'Locomotion')

        params = [('Speed', 'Float', 0.0), ('MotionSpeed', 'Float', 1.0), ('MoveX', 'Float', 0.0), ('MoveY', 'Float', 0.0),
                  ('Grounded', 'Bool', True), ('Jump', 'Bool', False), ('FreeFall', 'Bool', False),
                  ('Crouch', 'Bool', False), ('Strafe', 'Bool', False), ('Block', 'Bool', False), ('Dead', 'Bool', False),
                  ('Weapon', 'Int', 0), ('Direction', 'Int', 0),
                  ('Attack', 'Trigger', False), ('HeavyAttack', 'Trigger', False), ('BlockHit', 'Trigger', False),
                  ('Hit', 'Trigger', False), ('HitHard', 'Trigger', False), ('Dodge', 'Trigger', False)]
        pj = []
        for n, t, v in params:
            pj.append({'name': n, 'type': t, 'float': float(v) if t == 'Float' else 0.0, 'int': int(v) if t == 'Int' else 0,
                       'bool': bool(v) if t == 'Bool' else False})
        return {'layers': [{'name': 'Base Layer', 'weight': 1.0, 'defaultState': 'Locomotion',
                            'entry': [-330.0, 0.0], 'any': [-330.0, 200.0], 'exit': [420.0, 0.0],
                            'states': self.states, 'transitions': self.transitions}],
                'parameters': pj}

    def install(self):
        n = copied = 0
        for rel, src in self.files.items():
            out = os.path.join(self.project, *rel.split('/'))
            s = os.path.join(self.lib.package, *src.split('/'))
            os.makedirs(os.path.dirname(out), exist_ok=True)
            if not (os.path.exists(out) and os.path.getsize(out) == os.path.getsize(s)):
                shutil.copyfile(s, out)
                copied += 1
            n += 1
        # 표에서 빠진 옛 클립은 지운다 (이 세트 폴더 안만)
        root = os.path.join(self.project, *self.dest.split('/'))
        keep = {os.path.normcase(os.path.join(self.project, *r.split('/'))) for r in self.files}
        removed = 0
        for d, _, fs in os.walk(root):
            for f in fs:
                p = os.path.join(d, f)
                if f.lower().endswith('.fbx') and os.path.normcase(p) not in keep:
                    os.remove(p)
                    removed += 1
        return n, copied, removed


def cs_ident(s):
    s = re.sub(r'[^0-9A-Za-z_]', '_', s)
    return ('_' + s) if s[:1].isdigit() else s


def write_cs(path, actions, ctrl_rel):
    states = list(dict.fromkeys(a['state'].split('/')[0] for a in actions))   # Blend Tree 는 자식 대신 트리 이름
    lines = ['// 자동 생성: Tools/unity_import/humanoid_set.py — 직접 고치지 말고 표를 고쳐 다시 만든다',
             '// 휴머노이드 공용 애니메이션 (%s) 의 매개변수 · 상태 이름 · 도우미' % ctrl_rel.replace('\\', '/'),
             'using NovaEngine;', '',
             'public enum HumanoidWeapon { %s }' % ', '.join('%s = %d' % (n, i) for i, n in enumerate(WEAPON_NAMES)),
             '/// <summary>맞은 · 피하는 · 쓰러지는 쪽 (Direction 매개변수)</summary>',
             'public enum HumanoidDir { Front = 0, Back = 1, Left = 2, Right = 3 }', '',
             'public static class HumanoidAnim', '{',
             '    public const string Controller = "%s";' % ctrl_rel.replace('\\', '\\\\'), '',
             '    /// <summary>무기를 바꾼다 (Loco_무기 로 넘어간다). 꺼내기 동작은 PlayEquip</summary>',
             '    public static void SetWeapon(Animator a, HumanoidWeapon w) => a.SetInteger("Weapon", (int)w);',
             '    public static HumanoidWeapon GetWeapon(Animator a) => (HumanoidWeapon)a.GetInteger("Weapon");',
             '    /// <summary>약공격 — 연타하면 Attack1 → 2 → 3 → 4 연속기</summary>',
             '    public static void Attack(Animator a) => a.SetTrigger("Attack");',
             '    public static void HeavyAttack(Animator a) => a.SetTrigger("HeavyAttack");',
             '    public static void SetBlock(Animator a, bool on) => a.SetBool("Block", on);',
             '    public static void BlockHit(Animator a) => a.SetTrigger("BlockHit");',
             '    public static void SetCrouch(Animator a, bool on) => a.SetBool("Crouch", on);',
             '    /// <summary>옆걸음 (적 고정): MoveX · MoveY 는 몸 기준 속도 (m/s)</summary>',
             '    public static void SetStrafe(Animator a, bool on, float moveX = 0, float moveY = 0) { a.SetBool("Strafe", on); a.SetFloat("MoveX", moveX); a.SetFloat("MoveY", moveY); }',
             '    public static void Hit(Animator a, HumanoidDir d, bool hard = false) { a.SetInteger("Direction", (int)d); a.SetTrigger(hard ? "HitHard" : "Hit"); }',
             '    public static void Dodge(Animator a, HumanoidDir d) { a.SetInteger("Direction", (int)d); a.SetTrigger("Dodge"); }',
             '    public static void Die(Animator a, HumanoidDir d) { a.SetInteger("Direction", (int)d); a.SetBool("Dead", true); }',
             '    public static void Revive(Animator a) => a.SetBool("Dead", false);',
             '    /// <summary>이름으로 동작을 튼다 (끝나면 Locomotion 으로 돌아온다, 반복 동작은 다른 동작을 틀 때까지)</summary>',
             '    public static void Play(Animator a, string state, float fade = 0.15f) => a.CrossFade(state, fade);',
             '    public static void PlayEquip(Animator a, HumanoidWeapon w) { if (w != HumanoidWeapon.None) Play(a, w + "_Equip"); }',
             '    public static void PlayUnequip(Animator a, HumanoidWeapon w) { if (w != HumanoidWeapon.None) Play(a, w + "_Unequip"); }',
             '    public static string State(Animator a) => a.GetCurrentAnimatorStateInfo(0).name;', '',
             '    /// <summary>상태 이름 (%d 개)</summary>' % len(states),
             '    public static class States', '    {']
    for s in states:
        lines.append('        public const string %s = "%s";' % (cs_ident(s), s))
    lines += ['    }', '}', '']
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8-sig', newline='\r\n') as f:
        f.write('\n'.join(lines))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--package', required=True, help='애니메이션 라이브러리 폴더 (catalog.json)')
    ap.add_argument('--project', required=True, help='NOVA 프로젝트 폴더')
    ap.add_argument('--dest', default='Assets/Animations/Humanoid')
    ap.add_argument('--name', default='HumanoidShared')
    ap.add_argument('--script-dir', default='Assets/Scripts/Humanoid')
    ap.add_argument('--dry-run', action='store_true')
    args = ap.parse_args()

    lib = Library(args.package)
    b = Builder(lib, args.project, args.dest, args.name)
    ctrl = b.build()
    ctrl_rel = '%s/%s.controller' % (b.dest, args.name)
    nstates = len(ctrl['layers'][0]['states'])
    print('states %d, transitions %d, clips %d' % (nstates, len(ctrl['layers'][0]['transitions']), len(b.files)))
    if lib.missing:
        print('없는 클립 %d (건너뜀):' % len(lib.missing))
        for m in lib.missing:
            print('   ', m)
    total = sum(os.path.getsize(os.path.join(args.package, *s.split('/'))) for s in b.files.values())
    print('크기 %.0f MB' % (total / 1e6))
    if args.dry_run:
        return
    n, copied, removed = b.install()
    out = os.path.join(args.project, *ctrl_rel.split('/'))
    with open(out, 'w', encoding='utf-8') as f:
        json.dump(ctrl, f, ensure_ascii=False, indent=1)
    with open(os.path.splitext(out)[0] + '.actions.json', 'w', encoding='utf-8') as f:
        json.dump({'controller': ctrl_rel, 'weapons': WEAPON_NAMES, 'directions': DIRS, 'states': b.actions}, f, ensure_ascii=False, indent=1)
    write_cs(os.path.join(args.project, *args.script_dir.split('/'), 'HumanoidAnim.cs'), b.actions, ctrl_rel.replace('/', '\\'))
    print('installed %d clips (copied %d, removed %d) -> %s' % (n, copied, removed, out))


if __name__ == '__main__':
    main()
