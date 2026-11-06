#!/bin/bash
# CuttyTerm SSH 인증 실기 테스트용 sshd 환경 (WSL Ubuntu, root 로 실행).
# 시스템 기본 sshd 설정(/etc/ssh/sshd_config)은 건드리지 않고, 테스트 전용 설정 3개를 만든다.
#   2222: 비밀번호 인증만        2223: 키보드 대화형(PAM)만        2224: 공개키만
# 실행 후 각 포트의 sshd 는 별도로 띄운다:  /usr/sbin/sshd -D -f /etc/ssh/cuttyterm_test_<port>.conf
# 사용법: tools/wsl_ssh_test_setup.sh <클라이언트 키를 복사할 폴더(WSL 경로)>
#
# 아래 값은 이 로컬 테스트 전용 계정/키에만 쓰이는 테스트 값이다.
TEST_USER=cuttytest
TEST_PASSWORD='CuttyTest-Pw-2026'
TEST_KEY_PASSPHRASE='CuttyTest-Key-2026'

set -e
OUT_DIR="${1:?클라이언트 키를 복사할 폴더를 지정하세요}"
mkdir -p "$OUT_DIR" /run/sshd

# 테스트 계정 (이미 있으면 비밀번호만 다시 설정)
id "$TEST_USER" >/dev/null 2>&1 || useradd -m -s /bin/bash "$TEST_USER"
echo "$TEST_USER:$TEST_PASSWORD" | chpasswd

# 클라이언트 키: ed25519(암호 없음), ed25519(암호 있음), RSA PEM 형식(암호 없음)
KEY_DIR=/root/cuttyterm_test_keys
rm -rf "$KEY_DIR"; mkdir -p "$KEY_DIR"
ssh-keygen -q -t ed25519 -N '' -C cuttyterm-plain -f "$KEY_DIR/id_test_plain"
ssh-keygen -q -t ed25519 -N "$TEST_KEY_PASSPHRASE" -C cuttyterm-enc -f "$KEY_DIR/id_test_enc"
ssh-keygen -q -t rsa -b 3072 -m PEM -N '' -C cuttyterm-rsa-pem -f "$KEY_DIR/id_test_rsa_pem"
HOME_DIR=$(getent passwd "$TEST_USER" | cut -d: -f6)
mkdir -p "$HOME_DIR/.ssh"
cat "$KEY_DIR"/*.pub > "$HOME_DIR/.ssh/authorized_keys"
chown -R "$TEST_USER:$TEST_USER" "$HOME_DIR/.ssh"
chmod 700 "$HOME_DIR/.ssh"; chmod 600 "$HOME_DIR/.ssh/authorized_keys"
cp "$KEY_DIR"/id_test_plain "$KEY_DIR"/id_test_enc "$KEY_DIR"/id_test_rsa_pem "$OUT_DIR/"

# 포트별 sshd 설정
ssh-keygen -A >/dev/null
write_conf() # <port> <password> <kbdint> <pubkey>
{
    cat > "/etc/ssh/cuttyterm_test_$1.conf" <<EOF
Port $1
ListenAddress 127.0.0.1
HostKey /etc/ssh/ssh_host_ed25519_key
HostKey /etc/ssh/ssh_host_rsa_key
PidFile /tmp/cuttyterm_sshd_$1.pid
LogLevel VERBOSE
UsePAM yes
AllowUsers $TEST_USER
PasswordAuthentication $2
KbdInteractiveAuthentication $3
PubkeyAuthentication $4
AuthorizedKeysFile .ssh/authorized_keys
EOF
    /usr/sbin/sshd -t -f "/etc/ssh/cuttyterm_test_$1.conf"
}
write_conf 2222 yes no no
write_conf 2223 no yes no
write_conf 2224 no no yes
echo "setup ok"
