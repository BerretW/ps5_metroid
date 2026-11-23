#requires -Version 5.1

function Initialize-WindowsVulkanSdk {
    if (-not $env:VULKAN_SDK) {
        foreach ($scope in @("User", "Machine")) {
            $sdk = [Environment]::GetEnvironmentVariable("VULKAN_SDK", $scope)
            if ($sdk) {
                $env:VULKAN_SDK = $sdk
                break
            }
        }
    }
}
