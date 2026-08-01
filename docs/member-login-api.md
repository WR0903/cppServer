# 账号验证接口（Nginx + PHP）

本项目通过 Nginx 反向代理到 PHP，对外提供 HTTP 账号验证接口，供 C++ 游戏服务端在玩家登录时校验账号密码。接口实际由部署在 `114.132.58.120` 上的 PHP 脚本处理。

---

## 1. 接口地址

| 项目     | 值                                 |
| -------- | ---------------------------------- |
| 协议     | HTTP                               |
| URL      | `http://114.132.58.120/member_login.php` |
| 方法     | GET / POST 均可（使用 `$_REQUEST`） |
| 编码     | UTF-8                              |
| 返回格式 | JSON（`text/html`）                |

---

## 2. 请求参数

| 参数名   | 类型   | 必填 | 说明                         |
| -------- | ------ | ---- | ---------------------------- |
| account  | string | 是   | 用户登录账号                 |
| password | string | 是   | 用户密码（MD5 后的密文）      |

**请求示例：**

```
GET http://114.132.58.120/member_login.php?account=test&password=e10adc3949ba59abbe56e057f20f883e
```

---

## 3. 返回结果

返回体为单字段 JSON：

```json
{"returncode": 0}
```

| returncode | 含义             |
| ---------- | ---------------- |
| 0          | 账号密码正确     |
| 1          | 数据库操作错误   |
| 2          | 用户名不存在     |
| 3          | 密码错误         |

---

## 4. 服务端处理流程

```
请求到达 member_login.php
  │
  ├─ 1. 连接 MySQL（account 库，127.0.0.1）
  │
  ├─ 2. 按传入 account 查询 account 表的 password 字段
  │     SELECT password FROM account.account WHERE account = '<account>'
  │
  ├─ 3. 若 SQL 出错            → {"returncode": 1}
  │
  ├─ 4. 若查不到记录            → {"returncode": 2}
  │
  └─ 5. 比对密码
        ├─ 相等 → {"returncode": 0}
        └─ 不等 → {"returncode": 3}
```

---

## 5. 数据库表结构

对应 `account.account` 表（见 `account.sql`）：

```sql
CREATE TABLE `account` (
  `id`       bigint(64)   NOT NULL AUTO_INCREMENT,
  `account`  varchar(20)  NOT NULL,
  `password` varchar(64)  NOT NULL,          -- 存放 MD5 密文
  PRIMARY KEY (`id`),
  UNIQUE KEY `account_index` (`account`) USING BTREE
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
```

测试账号示例：

| account | password（MD5）                     | 明文   |
| ------- | ----------------------------------- | ------ |
| test    | e10adc3949ba59abbe56e057f20f883e    | 123456 |

---

## 6. 测试接口

服务器另提供测试用脚本 `member_login_t.php`，**不查数据库**，直接返回成功：

```
http://114.132.58.120/member_login_t.php
→ {"returncode":0}
```

用于联调阶段绕过数据库验证。

---

## 7. 关联接口：注册

`member_register.php` 提供账号注册能力（注意该脚本仍使用旧的 `mysql_*` 函数，仅供参考）：

- **URL**: `http://114.132.58.120/member_register.php`
- **方法**: POST
- **参数**:

| 参数名   | 说明               |
| -------- | ------------------ |
| username | 注册用户名         |
| password | MD5 后的密码        |

- **返回**:

```json
{"returnCode": "1", "returnMessage": ""}
```

| returnCode | 含义           |
| ---------- | -------------- |
| 1          | 操作成功       |
| 2          | 用户名被占用   |
| 3          | 数据库操作出错 |

---

## 8. 文件清单（服务器 `/opt/www/`）

| 文件                  | 作用                               |
| --------------------- | ---------------------------------- |
| `member_login.php`    | 登录验证主接口                     |
| `member_login_t.php`  | 测试用接口，固定返回成功           |
| `member_register.php` | 账号注册接口                       |
| `mysql_config.php`    | MySQL 连接配置（账号库）           |
| `account.sql`         | `account` 表结构与测试数据导出     |

---

## 9. 安全说明

- `member_login.php` 中 SQL 使用字符串拼接（`trim($_REQUEST['account'])`），存在 SQL 注入风险，生产环境建议改用预处理语句。
- 明文 HTTP 传输，密码以 MD5 形式传递，建议升级为 HTTPS。
- `mysql_config.php` 中数据库密码硬编码，建议改为环境变量。

---

## 10. Nginx 安装与配置

### 10.1 环境信息

| 组件  | 版本                                  |
| ----- | ------------------------------------- |
| OS    | Ubuntu 24.04 LTS                      |
| Nginx | nginx/1.24.0 (Ubuntu)                 |
| PHP   | 8.3.6 (PHP-FPM)                       |
| MySQL | 8.0.45-0ubuntu0.24.04.1               |

### 10.2 安装 Nginx

```bash
sudo apt update
sudo apt install -y nginx
```

安装后 Nginx 自动启动：

```bash
sudo systemctl enable nginx     # 开机自启
sudo systemctl status nginx     # 查看状态
```

### 10.3 安装 PHP-FPM

```bash
sudo apt install -y php8.3-fpm php8.3-mysql
```

确认 `mysqli` 扩展已加载（`member_login.php` 依赖它）：

```bash
php -m | grep mysqli
# 输出: mysqli
```

### 10.4 Nginx 配置

主配置 `/etc/nginx/nginx.conf`（Ubuntu 默认，无需修改）：

```nginx
user www-data;
worker_processes auto;
pid /run/nginx.pid;
error_log /var/log/nginx/error.log;
include /etc/nginx/modules-enabled/*.conf;

events {
    worker_connections 768;
}

http {
    include /etc/nginx/mime.types;
    default_type application/octet-stream;
    sendfile on;
    include /etc/nginx/conf.d/*.conf;
    include /etc/nginx/sites-enabled/*;
}
```

站点配置 `/etc/nginx/conf.d/www.conf`：

```nginx
server {
    server_name 127.0.0.1;
    index  index.php;
    root   /opt/www;              # PHP 脚本目录
    include php-fpm.conf;         # 引入 PHP-FPM 转发规则
}
```

PHP-FPM 转发配置 `/etc/nginx/php-fpm.conf`：

```nginx
location ~ .*(\.php)$ {
    fastcgi_pass    unix:/run/php/php8.3-fpm.sock;
    fastcgi_param   PHP_SELF         $uri;
    fastcgi_param   SERVER_NAME      $host;
    fastcgi_param   SCRIPT_FILENAME  $document_root$fastcgi_script_name;
    fastcgi_index   index.php;
    include fastcgi_params;
    include php_cgi.conf;
}
```

> Nginx 通过 Unix Socket (`/run/php/php8.3-fpm.sock`) 将 `.php` 请求转发给 PHP-FPM 处理。

### 10.5 验证配置并重载

```bash
sudo nginx -t              # 检查配置语法
sudo systemctl reload nginx
```

---

## 11. PHP-FPM 配置

### 11.1 进程池配置

配置文件：`/etc/php/8.3/fpm/pool.d/www.conf`

关键配置项：

```ini
[global]
pid = /run/php/php8.3-fpm.pid
error_log = /var/log/php8.3-fpm-error.log

[www]
listen = /run/php/php8.3-fpm.sock   ; 与 Nginx 中的 fastcgi_pass 对应
listen.owner = www-data
listen.group = www-data
user = www-data
group = www-data
pm = static                          ; 静态进程管理模式
pm.max_children = 5                  ; 最大子进程数
rlimit_files = 20000                 ; 文件描述符上限
request_terminate_timeout = 10       ; 请求超时 10 秒
pm.max_requests = 20000             ; 每个进程处理 20000 请求后重启（防内存泄漏）
```

### 11.2 服务管理

```bash
sudo systemctl enable php8.3-fpm     # 开机自启
sudo systemctl start php8.3-fpm
sudo systemctl restart php8.3-fpm    # 修改配置后重启
sudo systemctl status php8.3-fpm
```

---

## 12. MySQL 配置

### 12.1 安装

```bash
sudo apt install -y mysql-server
sudo systemctl enable mysql
sudo systemctl start mysql
```

### 12.2 创建 account 库与表

```bash
mysql -u root -p
```

```sql
CREATE DATABASE account CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;

USE account;

CREATE TABLE `account` (
  `id`       bigint       NOT NULL AUTO_INCREMENT,
  `account`  varchar(20)  NOT NULL,
  `password` varchar(64)  NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `account_index` (`account`) USING BTREE
) ENGINE=InnoDB DEFAULT CHARSET=utf8;
```

也可直接导入服务器上已有的 `account.sql`：

```bash
mysql -u root -p < /opt/www/account.sql
```

### 12.3 插入测试账号

```sql
-- 明文 123456 的 MD5
INSERT INTO account (account, password) VALUES ('test', 'e10adc3949ba59abbe56e057f20f883e');
```

批量生成测试账号（利用存储过程）：

```sql
CALL gen_account();  -- 生成 test1 ~ test1000，密码均为 123456
```

### 12.4 PHP 连接配置

PHP 端通过 `/opt/www/mysql_config.php` 连接 MySQL：

```php
<?php
$dbsource['account']['address']  = '127.0.0.1';
$dbsource['account']['username'] = 'root';
$dbsource['account']['password'] = '260386';
$dbsource['dbname']              = 'account';

$db = mysqli_connect(
    $dbsource['account']['address'],
    $dbsource['account']['username'],
    $dbsource['account']['password'],
    $dbsource['dbname']
);
mysqli_query($db, 'SET NAMES UTF8');
?>
```

| 配置项 | 值         | 说明                        |
| ------ | ---------- | --------------------------- |
| 地址   | 127.0.0.1  | MySQL 与 Nginx/PHP 同机部署 |
| 用户   | root       | 数据库用户名                |
| 密码   | 260386     | 数据库密码                  |
| 库名   | account    | 账号库                      |

### 12.5 验证 MySQL 连接

```bash
mysql -u root -p260386 -e "SELECT id, account, password FROM account.account LIMIT 5;"
```

```
+------+---------+----------------------------------+
| id   | account | password                         |
+------+---------+----------------------------------+
| 1000 | test    | e10adc3949ba59abbe56e057f20f883e |
| 1001 | test1   | e10adc3949ba59abbe56e057f20f883e |
| 1002 | test2   | e10adc3949ba59abbe56e057f20f883e |
| 1003 | test3   | e10adc3949ba59abbe56e057f20f883e |
| 1004 | test4   | e10adc3949ba59abbe56e057f20f883e |
+------+---------+----------------------------------+
```

---

## 13. 完整部署流程（从零搭建）

```bash
# 1. 安装组件
sudo apt update
sudo apt install -y nginx php8.3-fpm php8.3-mysql mysql-server

# 2. 启动 MySQL 并建库建表
sudo systemctl start mysql
mysql -u root -p < /opt/www/account.sql

# 3. 部署 PHP 脚本
sudo mkdir -p /opt/www
sudo cp member_login.php member_login_t.php member_register.php mysql_config.php /opt/www/

# 4. 配置 Nginx（写入 /etc/nginx/conf.d/www.conf 和 /etc/nginx/php-fpm.conf）
#    （内容见第 10.4 节）

# 5. 启动 PHP-FPM 和 Nginx
sudo systemctl start php8.3-fpm
sudo systemctl start nginx

# 6. 验证
curl "http://127.0.0.1/member_login.php?account=test&password=e10adc3949ba59abbe56e057f20f883e"
# 期望输出: {"returncode":0}
```

### 13.1 服务状态一览

```bash
sudo systemctl status nginx php8.3-fpm mysql
```

三个服务均应为 `active (running)`。
