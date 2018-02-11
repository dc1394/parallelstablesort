﻿/*! \file parallelstablesort.cpp
	\brief スレッド並列化した安定ソートのパフォーマンスをチェックする

	Copyright © 2018 @dc1394 All Rights Reserved.
	This software is released under the BSD 2-Clause License.
*/

#include <algorithm>				// for std::inplace_merge, std::stable_sort
#include <chrono>					// for std::chrono
#include <cstdint>					// for std::int32_t
#include <fstream>					// for std::ofstream
#include <iostream>					// for std::cout, std::cerr
#include <iterator>                 // for std::distance
#include <random>					// for std::mt19937, std::random_device
#include <thread>					// for std::thread
#include <utility>					// for std::make_pair, std::pair
#include <vector>					// for std::vector

#if __INTEL_COMPILER >= 18
#include <pstl/algorithm>
#include <pstl/execution>			// for std::execution::par_unseq
#endif

#include <boost/assert.hpp>			// for boost::assert
#include <boost/format.hpp>			// for boost::format
#include <boost/thread.hpp>         // for boost::thread::physical_concurrency

#if defined(__INTEL_COMPILER) || __GNUC__ >= 5
#include <cilk/cilk.h>				// for cilk_spawn, cilk_sync
#endif

#include <tbb/parallel_invoke.h>	// for tbb::parallel_invoke

namespace {
    // #region 型エイリアス

    using mypair = std::pair<std::int32_t, std::int32_t>;

    // #endregion 型エイリアス

    //! A enumerated type
    /*!
        パフォーマンスをチェックする際の対象配列の種類を定義した列挙型
    */
    enum class Checktype : std::int32_t {
        // 完全にランダムなデータ
        RANDOM = 0,

        // あらかじめソートされたデータ
        SORT = 1,

        // 最初の1/4だけソートされたデータ
        QUARTERSORT = 2
    };

    //! A global variable (constant expression).
    /*!
        計測する回数
    */
    static auto constexpr CHECKLOOP = 10;

    //! A global variable (constant expression).
    /*!
        ソートする配列の要素数の最初の数
    */
    static auto constexpr N = 100;

    //! A global variable (constant).
    /*!
        再帰するかどうかの閾値
    */
    static auto const THRESHOLD = 3;
        
    //! A function.
    /*!
        並列化されたソート関数のパフォーマンスをチェックする
        \param checktype パフォーマンスをチェックする際の対象配列の種類
        \param ofs 出力用のファイルストリーム
    */
    void check_performance(Checktype checktype, std::ofstream & ofs);

    //! A function.
    /*!
        引数で与えられたstd::functionの実行時間をファイルに出力する
        \param checktype パフォーマンスをチェックする際の対象配列の種類
        \param distribution 乱数の分布
        \param func 実行するstd::function
        \param n 配列のサイス
        \param ofs 出力用のファイルストリーム
        \param randengine 乱数生成エンジン
    */
    void elapsed_time(Checktype checktype, std::uniform_int_distribution<std::int32_t> & distribution, std::function<void(std::vector<mypair> &)> const & func, std::int32_t n, std::ofstream & ofs, std::mt19937 & randengine);

#if defined(__INTEL_COMPILER) || __GNUC__ >= 5
    template < class RandomIter >
    //! A template function.
    /*!
        指定された範囲の要素を安定ソートする
        \param first 範囲の下限
        \param last 範囲の上限
        \param reci 現在の再帰の深さ
    */
    void stable_sort_cilk(RandomIter first, RandomIter last, std::int32_t reci)
    {
        // 部分ソートの要素数
        auto const len = std::distance(first, last);

        if (len <= 1) {
            // 部分ソートの要素数が1個以下なら何もすることはない
            return;
        }

        // 再帰の深さ + 1
        reci++;

        // 現在の再帰の深さが閾値以下のときだけ並列化させる
        if (reci <= THRESHOLD) {
            auto middle = first + len / 2;

            // 下部をソート（別スレッドで実行）
            cilk_spawn stable_sort_cilk(first, middle, reci);

            // 上部をソート（別スレッドで実行）
            cilk_spawn stable_sort_cilk(middle, last, reci);

            // 二つのスレッドの終了を待機
            cilk_sync;

            // ソートされた下部と上部をマージ
            std::inplace_merge(first, middle, last);
        }
        else {
            // C++標準の安定ソートの関数を呼び出す
            std::stable_sort(first, last);
        }
    }

    template < class RandomIter >
    //! A template function.
    /*!
        指定された範囲の要素を安定ソートする（Cilkで並列化）
        \param first 範囲の下限
        \param last 範囲の上限
    */
    inline void stable_sort_cilk(RandomIter first, RandomIter last)
    {
        // 再帰ありの並列安定ソートの関数を呼び出す
        stable_sort_cilk(first, last, 0);
    }
#endif

#if _OPENMP >= 200805
    template < class RandomIter >
    //! A template function.
    /*!
        指定された範囲の要素を安定ソートする（OpenMPで並列化）
        \param first 範囲の下限
        \param last 範囲の上限
        \param reci 現在の再帰の深さ
    */
    void stable_sort_openmp(RandomIter first, RandomIter last, std::int32_t reci)
    {
        // 部分ソートの要素数
        auto const len = std::distance(first, last);

        if (len <= 1) {
            // 部分ソートの要素数が1個以下なら何もすることはない
            return;
        }

        // 再帰の深さ + 1
        reci++;

        // 現在の再帰の深さが閾値以下のときだけ並列化させる
        if (reci <= THRESHOLD) {
            auto middle = first + len / 2;

            // 次の関数をタスクとして実行
#pragma omp task
            // 下部をソート
            stable_sort_openmp(first, middle, reci);

            // 次の関数をタスクとして実行
#pragma omp task
            // 上部をソート
            stable_sort_openmp(middle, last, reci);

            // 二つのタスクの終了を待機
#pragma omp taskwait

            // ソートされた下部と上部をマージ
            std::inplace_merge(first, middle, last);
        }
        else {
            // C++標準の安定ソートの関数を呼び出す
            std::stable_sort(first, last);
        }
    }

    template < class RandomIter >
    //! A template function.
    /*!
        指定された範囲の要素を安定ソートする（OpenMPで並列化）
        \param first 範囲の下限
        \param last 範囲の上限
    */
    inline void stable_sort_openmp(RandomIter first, RandomIter last)
    {
#pragma omp parallel    // OpenMP並列領域の始まり
#pragma omp single      // task句はsingle領域で実行
        // 再帰ありの並列安定ソートの関数を呼び出す
        stable_sort_openmp(first, last, 0);
    }
#endif

    template < class RandomIter >
    //! A template function.
    /*!
        指定された範囲の要素を安定ソートする
        \param first 範囲の下限
        \param last 範囲の上限
        \param reci 現在の再帰の深さ
    */
    void stable_sort_tbb(RandomIter first, RandomIter last, std::int32_t reci)
    {
        // 部分ソートの要素数
        auto const len = std::distance(first, last);

        if (len <= 1) {
            // 部分ソートの要素数が1個以下なら何もすることはない
            return;
        }

        // 再帰の深さ + 1
        reci++;

        // 現在の再帰の深さが閾値以下のときだけ並列化させる
        if (reci <= THRESHOLD) {
            auto middle = first + len / 2;

            // 二つのラムダ式を別スレッドで実行
            tbb::parallel_invoke(
                // 下部をソート
                [first, middle, reci]() { stable_sort_tbb(first, middle, reci); },
                // 上部をソート
                [middle, last, reci]() { stable_sort_tbb(middle, last, reci); });

            // ソートされた下部と上部をマージ
            std::inplace_merge(first, middle, last);
        }
        else {
            // C++標準の安定ソートの関数を呼び出す
            std::stable_sort(first, last);
        }
    }

    template < class RandomIter >
    //! A template function.
    /*!
        指定された範囲の要素を安定ソートする（TBBで並列化）
        \param first 範囲の下限
        \param last 範囲の上限
    */
    inline void stable_sort_tbb(RandomIter first, RandomIter last)
    {
        // 再帰ありの並列安定ソートの関数を呼び出す
        stable_sort_tbb(first, last, 0);
    }

    template < class RandomIter >
    //! A template function.
    /*!
        指定された範囲の要素を安定ソートする
        \param first 範囲の下限
        \param last 範囲の上限
        \param reci 現在の再帰の深さ
    */
    void stable_sort_thread(RandomIter first, RandomIter last, std::int32_t reci)
    {
        // 部分ソートの要素数
        auto const len = std::distance(first, last);

        if (len <= 1) {
            // 部分ソートの要素数が1個以下なら何もすることはない
            return;
        }

        // 再帰の深さ + 1
        reci++;

        // 現在の再帰の深さが閾値以下のときだけ並列化させる
        if (reci <= THRESHOLD) {
            auto middle = first + len / 2;

            // 下部をソート（別スレッドで実行）
            auto th1 = std::thread([first, middle, reci]() { stable_sort_thread(first, middle, reci); });

            // 上部をソート（別スレッドで実行）
            auto th2 = std::thread([middle, last, reci]() { stable_sort_thread(middle, last, reci); });

            // 二つのスレッドの終了を待機
            th1.join();
            th2.join();

            // ソートされた下部と上部をマージ
            std::inplace_merge(first, middle, last);
        }
        else {
            // C++標準の安定ソートの関数を呼び出す
            std::stable_sort(first, last);
        }
    }

    template < class RandomIter >
    //! A template function.
    /*!
        指定された範囲の要素を安定ソートする（std::threadで並列化）
        \param first 範囲の下限
        \param last 範囲の上限
    */
    inline void stable_sort_thread(RandomIter first, RandomIter last)
    {
        // 再帰ありの並列安定ソートの関数を呼び出す
        stable_sort_thread(first, last, 0);
    }

#ifdef DEBUG
    //! A template function.
    /*!
        与えられた二つのstd::vectorのすべての要素が同じかどうかチェックする
        \param v1 一つ目のstd::vector
        \param v2 二つめのstd::vector
        \return 与えられた二つのstd::vectorのすべての要素が同じならtrue、そうでないならfalse
    */
    bool vec_check(std::vector<mypair> const & v1, std::vector<mypair> const & v2);
#endif
}

int main()
{
    std::cout << "物理コア数: " << boost::thread::physical_concurrency();
    std::cout << ", 論理コア数: " << boost::thread::hardware_concurrency() << std::endl;

    std::ofstream ofsrandom("完全にシャッフルされたデータ.csv");
    std::ofstream ofssort("あらかじめソートされたデータ.csv");
    std::ofstream ofsquartersort("最初の1_4だけソートされたデータ.csv");
    
    std::cout << "完全にシャッフルされたデータを計測中...\n";
    check_performance(Checktype::RANDOM, ofsrandom);

    std::cout << "\nあらかじめソートされたデータを計測中...\n";
    check_performance(Checktype::SORT, ofssort);

    std::cout << "\n最初の1_4だけソートされたデータを計測中...\n";
    check_performance(Checktype::QUARTERSORT, ofsquartersort);

    return 0;
}

namespace {
    void check_performance(Checktype checktype, std::ofstream & ofs)
    {
        ofs << "配列の要素数,std::stable_sort,std::thread,OpenMP,TBB,Cilk,std::stable_sort (Parallelism TS)\n";

        // ランダムデバイス
        std::random_device rnd;
        
        // 乱数エンジン
        auto randengine = std::mt19937(rnd());

        auto n = N;
        for (auto i = 0; i < 7; i++) {
            for (auto j = 0; j < 2; j++) {
                std::cout << n << "個を計測中...\n";

                std::uniform_int_distribution<std::int32_t> distribution(1, n / 10);

                ofs << n << ',';

                elapsed_time(checktype, distribution, [](auto & vec) { std::stable_sort(vec.begin(), vec.end()); }, n, ofs, randengine);
                elapsed_time(checktype, distribution, [](auto & vec) { stable_sort_thread(vec.begin(), vec.end()); }, n, ofs, randengine);

#if _OPENMP >= 200805
                elapsed_time(checktype, distribution, [](auto & vec) { stable_sort_openmp(vec.begin(), vec.end()); }, n, ofs, randengine);
#endif
                elapsed_time(checktype, distribution, [](auto & vec) { stable_sort_tbb(vec.begin(), vec.end()); }, n, ofs, randengine);

#if defined(__INTEL_COMPILER) || __GNUC__ >= 5
                elapsed_time(checktype, distribution, [](auto & vec) { stable_sort_cilk(vec.begin(), vec.end()); }, n, ofs, randengine);
#endif

#if __INTEL_COMPILER >= 18
                elapsed_time(checktype, distribution, [](auto & vec) { std::stable_sort(std::execution::par, vec.begin(), vec.end()); }, n, ofs, randengine);
#endif
                ofs << std::endl;

                if (i == 6) {
                    break;
                }
                else if (!j) {
                    n *= 5;
                }
            }

            n *= 2;
        }
    }

    void elapsed_time(Checktype checktype, std::uniform_int_distribution<std::int32_t> & distribution, std::function<void(std::vector<mypair> &)> const & func, std::int32_t n, std::ofstream & ofs, std::mt19937 & randengine)
    {
        using namespace std::chrono;

        std::vector<mypair> vec(n);

        auto elapsed_time = 0.0;
        for (auto i = 1; i <= CHECKLOOP; i++) {
            for (auto j = 0; j < n; j++) {
                vec[j] = std::make_pair(distribution(randengine), j);
            }

            switch (checktype) {
            case Checktype::RANDOM:
                break;

            case Checktype::SORT:
#if __INTEL_COMPILER >= 18
                std::stable_sort(std::execution::par_unseq, vec.begin(), vec.end());
#else
                std::stable_sort(vec.begin(), vec.end());
#endif
                break;

            case Checktype::QUARTERSORT:
#if __INTEL_COMPILER >= 18
                std::stable_sort(std::execution::par_unseq, vec.begin(), vec.begin() + n / 4);
#else
                std::stable_sort(vec.begin(), vec.begin() + n / 4);
#endif
                break;

            default:
                BOOST_ASSERT(!"switchのdefaultに来てしまった！");
                break;
            }
            
            auto const beg = high_resolution_clock::now();
            func(vec);
            auto const end = high_resolution_clock::now();

            elapsed_time += (duration_cast<duration<double>>(end - beg)).count();
        }

        ofs << boost::format("%.10f") % (elapsed_time / static_cast<double>(CHECKLOOP)) << ',';


#ifdef DEBUG
        std::vector<mypair> vecback(vec);

#if __INTEL_COMPILER >= 18
            std::stable_sort(std::execution::par_unseq, vecback.begin(), vecback.end());
#else
            std::stable_sort(vecback.begin(), vecback.end());
#endif

        if (!vec_check(vec, vecback)) {
            std::cerr << "エラー発見！" << std::endl;
        }
#endif
    }
    
#ifdef DEBUG
    bool vec_check(std::vector<mypair> const & v1, std::vector<mypair> const & v2)
    {
        auto const size = v1.size();
        BOOST_ASSERT(size == v2.size());

        for (auto i = 0; i < size; i++) {
            if (v1[i] != v2[i]) {
                std::cerr << "Error! i = " << i << std::endl;
                return false;
            }
        }

        return true;
    }
#endif
}