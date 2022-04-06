# -*- mode: ruby -*-
# vi: set ft=ruby :

Vagrant.configure("2") do |config|
  config.vm.box = "bento/ubuntu-20.04"

  config.vm.provision "shell", privileged: false, inline: <<-SHELL
	/vagrant/scripts/ubuntu_18.04_dependencies.sh

  echo "export PATH=$PATH:$HOME/.cargo/bin" >> $HOME/.bash_profile

  SHELL
end
