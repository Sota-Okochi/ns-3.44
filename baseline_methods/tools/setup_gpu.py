#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
GPU高速化版ハンガリアン法のセットアップと動作確認スクリプト
"""

import sys
import subprocess
import importlib


def check_cuda_installation():
    """CUDA環境の確認"""
    print("=== CUDA環境の確認 ===")
    try:
        result = subprocess.run(['nvidia-smi'], capture_output=True, text=True)
        if result.returncode == 0:
            print("✅ NVIDIA GPUが検出されました")
            print(result.stdout.split('\n')[0:3])  # ヘッダー部分を表示
            return True
        else:
            print("❌ nvidia-smiコマンドが失敗しました")
            return False
    except FileNotFoundError:
        print("❌ nvidia-smiが見つかりません。CUDAがインストールされていない可能性があります")
        return False


def check_python_packages():
    """必要なPythonパッケージの確認"""
    print("\n=== Pythonパッケージの確認 ===")

    required_packages = ['numpy', 'scipy']
    optional_packages = ['cupy']

    installed_packages = {}
    missing_packages = []

    # 必須パッケージの確認
    for package in required_packages:
        try:
            module = importlib.import_module(package)
            version = getattr(module, '__version__', 'Unknown')
            installed_packages[package] = version
            print(f"✅ {package}: {version}")
        except ImportError:
            missing_packages.append(package)
            print(f"❌ {package}: 未インストール")

    # CuPyの確認
    cupy_available = False
    try:
        import cupy as cp
        installed_packages['cupy'] = cp.__version__
        print(f"✅ cupy: {cp.__version__}")
        cupy_available = True
    except ImportError:
        print("⚠️  cupy: 未インストール（GPU機能が使用できません）")

    return len(missing_packages) == 0, cupy_available, missing_packages


def test_gpu_functionality():
    """GPU機能のテスト"""
    print("\n=== GPU機能のテスト ===")

    try:
        import cupy as cp

        # GPU利用可能性確認
        if not cp.cuda.is_available():
            print("❌ CUDAが利用できません")
            return False

        print("✅ CUDAが利用可能です")

        # デバイス情報表示
        device_count = cp.cuda.runtime.getDeviceCount()
        print(f"検出されたGPU数: {device_count}")

        for i in range(device_count):
            with cp.cuda.Device(i):
                props = cp.cuda.runtime.getDeviceProperties(i)
                print(f"  GPU {i}: {props['name'].decode()}")
                print(f"    メモリ: {props['totalGlobalMem'] / (1024**3):.1f} GB")

        # 簡単な計算テスト
        print("\n簡単な計算テストを実行中...")
        cpu_array = cp.asnumpy(cp.arange(1000))
        gpu_array = cp.asarray(cpu_array)
        gpu_result = cp.sum(gpu_array ** 2)
        cpu_result = cp.asnumpy(gpu_result)

        expected = sum(i**2 for i in range(1000))
        if abs(cpu_result - expected) < 1e-6:
            print("✅ GPU計算テスト成功")
            return True
        else:
            print("❌ GPU計算テスト失敗")
            return False

    except Exception as e:
        print(f"❌ GPU機能テストエラー: {e}")
        return False


def install_missing_packages(missing_packages):
    """不足パッケージの自動インストール"""
    print(f"\n=== 不足パッケージのインストール ===")

    for package in missing_packages:
        print(f"{package}をインストール中...")
        try:
            subprocess.run([sys.executable, '-m', 'pip', 'install', package],
                           check=True, capture_output=True)
            print(f"✅ {package}のインストール完了")
        except subprocess.CalledProcessError as e:
            print(f"❌ {package}のインストール失敗: {e}")
            return False

    return True


def install_cupy():
    """CuPyの自動インストール"""
    print("\n=== CuPyのインストール ===")

    # CUDA バージョンの検出を試行
    cuda_version = None
    try:
        result = subprocess.run(['nvcc', '--version'],
                                capture_output=True, text=True)
        if result.returncode == 0:
            for line in result.stdout.split('\n'):
                if 'release' in line:
                    version_str = line.split('release')[
                        1].split(',')[0].strip()
                    cuda_version = version_str
                    break
    except FileNotFoundError:
        pass

    # CuPyパッケージ名の決定
    if cuda_version:
        major_version = cuda_version.split('.')[0]
        if major_version == '11':
            cupy_package = 'cupy-cuda11x'
        elif major_version == '12':
            cupy_package = 'cupy-cuda12x'
        else:
            cupy_package = 'cupy'
        print(f"CUDA {cuda_version}が検出されました。{cupy_package}をインストールします。")
    else:
        cupy_package = 'cupy'
        print("CUDAバージョンが検出できませんでした。汎用版のcupyをインストールします。")

    try:
        subprocess.run([sys.executable, '-m', 'pip', 'install', cupy_package],
                       check=True, capture_output=True)
        print(f"✅ {cupy_package}のインストール完了")
        return True
    except subprocess.CalledProcessError as e:
        print(f"❌ {cupy_package}のインストール失敗: {e}")

        # フォールバック: 汎用版を試行
        if cupy_package != 'cupy':
            print("汎用版のcupyを試行中...")
            try:
                subprocess.run([sys.executable, '-m', 'pip', 'install', 'cupy'],
                               check=True, capture_output=True)
                print("✅ cupy（汎用版）のインストール完了")
                return True
            except subprocess.CalledProcessError:
                print("❌ cupy（汎用版）のインストールも失敗")

        return False


def main():
    """メイン関数"""
    print("ハンガリアン法GPU高速化版 セットアップスクリプト")
    print("=" * 50)

    # CUDA環境確認
    cuda_available = check_cuda_installation()

    # Pythonパッケージ確認
    packages_ok, cupy_available, missing_packages = check_python_packages()

    # 不足パッケージのインストール
    if missing_packages:
        user_input = input(
            f"\n不足パッケージ（{missing_packages}）をインストールしますか？ [y/N]: ")
        if user_input.lower() in ['y', 'yes']:
            if not install_missing_packages(missing_packages):
                print("パッケージインストールに失敗しました。手動でインストールしてください。")
                return

    # CuPyのインストール
    if cuda_available and not cupy_available:
        user_input = input("\nCuPy（GPU計算ライブラリ）をインストールしますか？ [y/N]: ")
        if user_input.lower() in ['y', 'yes']:
            if install_cupy():
                cupy_available = True

    # GPU機能テスト
    if cuda_available and cupy_available:
        gpu_test_ok = test_gpu_functionality()
    else:
        gpu_test_ok = False

    # 結果サマリー
    print("\n" + "=" * 50)
    print("セットアップ結果サマリー")
    print("=" * 50)

    print(f"CUDA環境: {'✅ 利用可能' if cuda_available else '❌ 利用不可'}")
    print(f"必須パッケージ: {'✅ インストール済み' if packages_ok else '❌ 不足あり'}")
    print(f"CuPy: {'✅ 利用可能' if cupy_available else '❌ 未インストール'}")
    print(f"GPU機能: {'✅ 正常動作' if gpu_test_ok else '❌ 利用不可'}")

    if gpu_test_ok:
        print("\n🎉 GPU高速化版が使用可能です！")
        print("\n次のステップ:")
        print("1. benchmark_comparison.py を実行して性能を確認")
        print("2. 既存のコードでimport hungarian_kai_gpu を使用")
        print("3. README_GPU.md で詳細な使用方法を確認")
    elif cuda_available:
        print("\n⚠️  GPU環境は検出されましたが、CuPyの設定に問題があります")
        print("手動でCuPyをインストールしてください:")
        print("  pip install cupy-cuda11x  # CUDA 11.x系")
        print("  pip install cupy-cuda12x  # CUDA 12.x系")
    else:
        print("\n💡 GPU環境が利用できませんが、CPU版は使用可能です")
        print("元のhungarian_kai.pyをご使用ください")


if __name__ == "__main__":
    main()
