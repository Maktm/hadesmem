// Copyright (C) 2010-2015 Joshua Boyce
// See the file COPYING for copying permission.

#include "d3d11.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>

#include <windows.h>
#include <winnt.h>
#include <winternl.h>

#include <d3d11.h>

#include <hadesmem/config.hpp>
#include <hadesmem/detail/last_error_preserver.hpp>
#include <hadesmem/detail/winternl.hpp>
#include <hadesmem/find_procedure.hpp>
#include <hadesmem/patcher.hpp>
#include <hadesmem/process.hpp>
#include <hadesmem/region.hpp>

#include "callbacks.hpp"
#include "d3d11_device.hpp"
#include "dxgi.hpp"
#include "dxgi_swap_chain.hpp"
#include "helpers.hpp"
#include "hook_counter.hpp"
#include "main.hpp"
#include "module.hpp"

namespace
{
class D3D11Impl : public hadesmem::cerberus::D3D11Interface
{
public:
  virtual std::size_t RegisterOnRelease(
    std::function<hadesmem::cerberus::OnReleaseD3D11Callback> const& callback)
    final
  {
    auto& callbacks = hadesmem::cerberus::GetOnReleaseD3D11Callbacks();
    return callbacks.Register(callback);
  }

  virtual void UnregisterOnRelease(std::size_t id) final
  {
    auto& callbacks = hadesmem::cerberus::GetOnReleaseD3D11Callbacks();
    return callbacks.Unregister(id);
  }
};

std::unique_ptr<hadesmem::PatchDetour<decltype(&::D3D11CreateDevice)>>&
  GetD3D11CreateDeviceDetour() noexcept
{
  static std::unique_ptr<hadesmem::PatchDetour<decltype(&::D3D11CreateDevice)>>
    detour;
  return detour;
}

std::unique_ptr<
  hadesmem::PatchDetour<decltype(&::D3D11CreateDeviceAndSwapChain)>>&
  GetD3D11CreateDeviceAndSwapChainDetour() noexcept
{
  static std::unique_ptr<
    hadesmem::PatchDetour<decltype(&::D3D11CreateDeviceAndSwapChain)>> detour;
  return detour;
}

std::pair<void*, SIZE_T>& GetD3D11Module() noexcept
{
  static std::pair<void*, SIZE_T> module{nullptr, 0};
  return module;
}

std::uint32_t& GetD3D11CreateHookCount() noexcept
{
  static __declspec(thread) std::uint32_t in_hook = 0;
  return in_hook;
}

extern "C" HRESULT WINAPI
  D3D11CreateDeviceDetour(hadesmem::PatchDetourBase* detour,
                          IDXGIAdapter* adapter,
                          D3D_DRIVER_TYPE driver_type,
                          HMODULE software,
                          UINT flags,
                          const D3D_FEATURE_LEVEL* ptr_feature_levels,
                          UINT feature_levels,
                          UINT sdk_version,
                          ID3D11Device** device,
                          D3D_FEATURE_LEVEL* feature_level,
                          ID3D11DeviceContext** immediate_context) noexcept
{
  hadesmem::detail::LastErrorPreserver last_error_preserver;
  hadesmem::cerberus::HookCounter hook_counter{&GetD3D11CreateHookCount()};

  HADESMEM_DETAIL_TRACE_FORMAT_A(
    "Args: [%p] [%d] [%p] [%u] [%p] [%u] [%u] [%p] [%p] [%p].",
    adapter,
    driver_type,
    software,
    flags,
    ptr_feature_levels,
    feature_levels,
    sdk_version,
    device,
    feature_level,
    immediate_context);

  auto const d3d11_create_device =
    detour->GetTrampolineT<decltype(&D3D11CreateDevice)>();
  last_error_preserver.Revert();
  auto const ret = d3d11_create_device(adapter,
                                       driver_type,
                                       software,
                                       flags,
                                       ptr_feature_levels,
                                       feature_levels,
                                       sdk_version,
                                       device,
                                       feature_level,
                                       immediate_context);
  last_error_preserver.Update();

  HADESMEM_DETAIL_TRACE_FORMAT_A("Ret: [%ld].", ret);

  if (FAILED(ret))
  {
    HADESMEM_DETAIL_TRACE_A("Failed.");
    return ret;
  }

  HADESMEM_DETAIL_TRACE_A("Succeeded.");

  if (!device)
  {
    HADESMEM_DETAIL_TRACE_A("Invalid device out param pointer.");
    return ret;
  }

  auto const hook_count = hook_counter.GetCount();
  HADESMEM_DETAIL_ASSERT(hook_count > 0);
  if (hook_count == 1)
  {
    HADESMEM_DETAIL_TRACE_FORMAT_A("Proxying ID3D11Device. Device: [%p].",
                                   *device);
    *device = new hadesmem::cerberus::D3D11DeviceProxy{*device};
  }

  return ret;
}

extern "C" HRESULT WINAPI D3D11CreateDeviceAndSwapChainDetour(
  hadesmem::PatchDetourBase* detour,
  IDXGIAdapter* adapter,
  D3D_DRIVER_TYPE driver_type,
  HMODULE software,
  UINT flags,
  const D3D_FEATURE_LEVEL* ptr_feature_levels,
  UINT feature_levels,
  UINT sdk_version,
  const DXGI_SWAP_CHAIN_DESC* swap_chain_desc,
  IDXGISwapChain** swap_chain,
  ID3D11Device** device,
  D3D_FEATURE_LEVEL* feature_level,
  ID3D11DeviceContext** immediate_context) noexcept
{
  hadesmem::detail::LastErrorPreserver last_error_preserver;
  hadesmem::cerberus::HookCounter hook_counter{&GetD3D11CreateHookCount()};

  HADESMEM_DETAIL_TRACE_FORMAT_A(
    "Args: [%p] [%d] [%p] [%u] [%p] [%u] [%u] [%p] [%p] [%p] [%p] [%p].",
    adapter,
    driver_type,
    software,
    flags,
    ptr_feature_levels,
    feature_levels,
    sdk_version,
    swap_chain_desc,
    swap_chain,
    device,
    feature_level,
    immediate_context);

  auto const d3d11_create_device_and_swap_chain =
    detour->GetTrampolineT<decltype(&D3D11CreateDeviceAndSwapChain)>();
  last_error_preserver.Revert();
  auto const ret = d3d11_create_device_and_swap_chain(adapter,
                                                      driver_type,
                                                      software,
                                                      flags,
                                                      ptr_feature_levels,
                                                      feature_levels,
                                                      sdk_version,
                                                      swap_chain_desc,
                                                      swap_chain,
                                                      device,
                                                      feature_level,
                                                      immediate_context);
  last_error_preserver.Update();

  HADESMEM_DETAIL_TRACE_FORMAT_A("Ret: [%ld].", ret);

  if (SUCCEEDED(ret))
  {
    HADESMEM_DETAIL_TRACE_A("Succeeded.");

    auto const hook_count = hook_counter.GetCount();
    HADESMEM_DETAIL_ASSERT(hook_count > 0);

    if (swap_chain)
    {
      if (hook_count == 1)
      {
        HADESMEM_DETAIL_TRACE_A("Proxying IDXGISwapChain.");
        *swap_chain = new hadesmem::cerberus::DXGISwapChainProxy{*swap_chain};
      }
    }
    else
    {
      HADESMEM_DETAIL_TRACE_A("Invalid swap chain out param pointer.");
    }

    if (device)
    {
      if (hook_count == 1)
      {
        HADESMEM_DETAIL_TRACE_FORMAT_A("Proxying ID3D11Device. Device: [%p].",
                                       *device);
        *device = new hadesmem::cerberus::D3D11DeviceProxy{*device};
      }
    }
    else
    {
      HADESMEM_DETAIL_TRACE_A("Invalid device out param pointer.");
    }
  }
  else
  {
    HADESMEM_DETAIL_TRACE_A("Failed.");
  }

  return ret;
}
}

namespace hadesmem
{
namespace cerberus
{
Callbacks<OnReleaseD3D11Callback>& GetOnReleaseD3D11Callbacks()
{
  static Callbacks<OnReleaseD3D11Callback> callbacks;
  return callbacks;
}

D3D11Interface& GetD3D11Interface() noexcept
{
  static D3D11Impl d3d11_impl;
  return d3d11_impl;
}

void InitializeD3D11()
{
  auto& helper = GetHelperInterface();
  helper.InitializeSupportForModule(
    L"D3D11", DetourD3D11, UndetourD3D11, GetD3D11Module);
}

void DetourD3D11(HMODULE base)
{
  auto const& process = GetThisProcess();
  auto& module = GetD3D11Module();
  auto& helper = GetHelperInterface();
  if (helper.CommonDetourModule(process, L"D3D11", base, module))
  {
    DetourFunc(process,
               base,
               "D3D11CreateDevice",
               GetD3D11CreateDeviceDetour(),
               D3D11CreateDeviceDetour);
    DetourFunc(process,
               base,
               "D3D11CreateDeviceAndSwapChain",
               GetD3D11CreateDeviceAndSwapChainDetour(),
               D3D11CreateDeviceAndSwapChainDetour);
  }
}

void UndetourD3D11(bool remove)
{
  auto& module = GetD3D11Module();
  auto& helper = GetHelperInterface();
  if (helper.CommonUndetourModule(L"D3D11", module))
  {
    UndetourFunc(L"D3D11CreateDeviceAndSwapChain",
                 GetD3D11CreateDeviceAndSwapChainDetour(),
                 remove);
    UndetourFunc(L"D3D11CreateDevice", GetD3D11CreateDeviceDetour(), remove);

    module = std::make_pair(nullptr, 0);
  }
}
}
}
