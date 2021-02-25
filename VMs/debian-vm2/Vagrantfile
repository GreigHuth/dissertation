Vagrant.configure("2") do |config|
  config.vm.box = "generic/debian9" 
  
  config.vm .define :test_vm1 do |test_vm1|
     test_vm1.vm.network :private_network, :ip => "10.20.30.40"
  end

end
