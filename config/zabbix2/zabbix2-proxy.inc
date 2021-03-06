<?php
/*
	zabbix2-proxy.inc
	part of pfSense (https://www.pfSense.org/)
	Copyright (C) 2013 Danilo G. Baio
	Copyright (C) 2013 Marcello Coutinho
	Copyright (C) 2015 ESF, LLC
	All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	1. Redistributions of source code must retain the above copyright notice,
	   this list of conditions and the following disclaimer.

	2. Redistributions in binary form must reproduce the above copyright
	   notice, this list of conditions and the following disclaimer in the
	   documentation and/or other materials provided with the distribution.

	THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
	INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
	AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
	AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
	OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
	SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
	CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
	POSSIBILITY OF SUCH DAMAGE.
*/
require_once("util.inc");
require_once("functions.inc");
require_once("pkg-utils.inc");
require_once("globals.inc");

global $zabbix2_pkg_base;
$pfs_version = substr(trim(file_get_contents("/etc/version")), 0, 3);
if ($pfs_version == "2.2") {
	$zabbix2_pkg_base = "zabbix24";
} else {
	// pfSense 2.1 with zabbix 2.2
	$zabbix2_pkg_base = "zabbix22";
}
define('ZABBIX_PROXY_BASE', '/usr/pbi/' . $zabbix2_pkg_base . '-proxy-' . php_uname("m"));

function php_deinstall_zabbix2_proxy() {
	global $config, $g, $zabbix2_pkg_base;

	exec("/usr/bin/killall zabbix_proxy");
	unlink_if_exists(ZABBIX_PROXY_BASE . "/etc/" . $zabbix2_pkg_base . "/zabbix_proxy.conf");
	unlink_if_exists("/var/log/zabbix2/zabbix_proxy.log");
	unlink_if_exists("/var/run/zabbix2/zabbix2_proxy.pid");

	if (!is_array($config['installedpackages']['zabbixagent'])) {
		if (is_dir("/var/log/zabbix2")) {
			exec("/bin/rm -r /var/log/zabbix2/");
		}
		if (is_dir("/var/run/zabbix2")) {
			exec("/bin/rm -r /var/run/zabbix2/");
		}
		if (is_dir("/var/db/zabbix2")) {
			exec("/bin/rm -r /var/db/zabbix2/");
		}
	}
}

function validate_input_zabbix2_proxy($post, &$input_errors) {
	if  (isset($post['proxyenabled'])) {
		if (!is_numericint($post['serverport'])) {
			$input_errors[] = "'Server Port' value is not numeric.";
   		} elseif ($post['serverport'] < 1 || $post['serverport'] > 65535) {
			$input_errors[] = "You must enter a valid value for 'Server Port'.";
		}

		if (!preg_match("/\w+/", $post['hostname'])) {
			$input_errors[] = "Hostname field is required.";
		}

		if (!is_numericint($post['configfrequency'])) {
			$input_errors[] = "'Config Frequency' value is not numeric.";
		}
	}
}

function sync_package_zabbix2_proxy() {
	global $config, $g, $zabbix2_pkg_base;

	conf_mount_rw();

	// Check zabbix proxy config
	if (is_array($config['installedpackages']['zabbixproxy'])) {
		$zbproxy_config = $config['installedpackages']['zabbixproxy']['config'][0];
		if ($zbproxy_config['proxyenabled'] == "on") {
			$Mode = (is_numericint($zbproxy_config['proxymode']) ? $zbproxy_config['proxymode'] : 0);
			$AdvancedParams = base64_decode($zbproxy_config['advancedparams']);

			$zbproxy_conf_file = <<< EOF
Server={$zbproxy_config['server']}
ServerPort={$zbproxy_config['serverport']}
Hostname={$zbproxy_config['hostname']}
PidFile=/var/run/zabbix2/zabbix2_proxy.pid
DBName=/var/db/zabbix2/proxy.db
LogFile=/var/log/zabbix2/zabbix_proxy.log
ConfigFrequency={$zbproxy_config['configfrequency']}
FpingLocation=/usr/local/sbin/fping
# There's currently no fping6 (IPv6) dependency in the package,
# but if there was, the binary would likely also be in /usr/local/sbin.
Fping6Location=/usr/local/sbin/fping6
ProxyMode={$Mode}
{$AdvancedParams}

EOF;
			file_put_contents(ZABBIX_PROXY_BASE . "/etc/" . $zabbix2_pkg_base . "/zabbix_proxy.conf", strtr($zbproxy_conf_file, array("\r" => "")));
		}
	}

	$want_sysctls = array(
		'kern.ipc.shmall' => '2097152',
		'kern.ipc.shmmax' => '2147483648',
		'kern.ipc.semmsl' => '250'
	);
	$sysctls = array();
	// Check sysctl file values
	$sc_file="";
	if (file_exists("/etc/sysctl.conf")) {
		$sc = file("/etc/sysctl.conf");
		foreach ($sc as $line) {
			list($sysk, $sysv) = explode("=", $line, 2);
			if (preg_match("/\w/", $line) && !array_key_exists($sysk, $want_sysctls)) {
				$sc_file .= $line;
			}
		}
	}
	foreach ($want_sysctls as $ws => $wv) {
		$sc_file .= "{$ws}={$wv}\n";
		mwexec("/sbin/sysctl {$ws}={$wv}");
	}
	file_put_contents("/etc/sysctl.conf", $sc_file);

	// Check bootloader values
	$lt_file = "";
	$want_tunables = array(
		'kern.ipc.semopm' => '100',
		'kern.ipc.semmni' => '128',
		'kern.ipc.semmns' => '32000',
		'kern.ipc.shmmni' => '4096'
	);
	$tunables = array();
	if (file_exists("/boot/loader.conf")) {
		$lt = file("/boot/loader.conf");
		foreach ($lt as $line) {
			list($tunable, $val) = explode("=", $line, 2);
			if (preg_match("/\w/", $line) && !array_key_exists($tunable, $want_tunables)) {
				$lt_file .= $line;
			}
		}
	}
	foreach ($want_tunables as $wt => $wv) {
		$lt_file .= "{$wt}={$wv}\n";
	}
	file_put_contents("/boot/loader.conf", $lt_file);

	// Check startup script files
	// Create a few directories and ensure the sample files are in place
	if (!is_dir(ZABBIX_PROXY_BASE . "/etc/" . $zabbix2_pkg_base)) {
		mwexec("/bin/mkdir -p " . ZABBIX_PROXY_BASE . "/etc/" . $zabbix2_pkg_base);
	}

	$dir_checks = <<< EOF

	if [ ! -d /var/log/zabbix2 ]; then
		/bin/mkdir -p /var/log/zabbix2
		/usr/sbin/chmod 755 /var/log/zabbix2
	fi
	/usr/sbin/chown -R zabbix:zabbix /var/log/zabbix2

	if [ ! -d /var/run/zabbix2 ]; then
		/bin/mkdir -p /var/run/zabbix2
		/usr/sbin/chmod 755 /var/run/zabbix2
	fi
	/usr/sbin/chown -R zabbix:zabbix /var/run/zabbix2

	if [ ! -d /var/db/zabbix2 ]; then
		/bin/mkdir -p /var/db/zabbix2
		/usr/sbin/chmod 755 /var/db/zabbix2
	fi
	/usr/sbin/chown -R zabbix:zabbix /var/db/zabbix2

EOF;

	$zproxy_rcfile = "/usr/local/etc/rc.d/zabbix2_proxy.sh";
	if (is_array($zbproxy_config) && $zbproxy_config['proxyenabled'] == "on") {
		$zproxy_start = strtr($dir_checks, array("\r" => "")). "\necho \"Starting Zabbix Proxy\"...\n";
		$zproxy_start .= ZABBIX_PROXY_BASE . "/sbin/zabbix_proxy\n";

		$zproxy_stop  = "echo \"Stopping Zabbix Proxy\"\n";
		$zproxy_stop .= "/usr/bin/killall zabbix_proxy\n";
		$zproxy_stop .= "/bin/sleep 5\n";

		write_rcfile(array(
			"file" => "zabbix2_proxy.sh",
			"start" => $zproxy_start,
			"stop" => $zproxy_stop
			)
		);
		if (is_service_running("zabbix_proxy")) {
			restart_service("zabbix_proxy");
		} else {
			start_service("zabbix_proxy");
		}
	} else {
		if (is_service_running("zabbix_proxy")) {
			stop_service("zabbix_proxy");
		}
		unlink_if_exists($zproxy_rcfile);
	}

	conf_mount_ro();
}

?>
