#include <iostream>
#include <functional>
#include <memory>
#include <tuple>
#include <utility>
#include <type_traits>
#include <atomic>
#include <exception>

namespace stackless_coroutine {
	enum class operation {
		_suspend,
		_next,
		_return,
		_break,
		_continue,
		_done,
		_exception
	};

	namespace detail {
		struct value_base {
			virtual ~value_base() {};

		};
		template<class T>
		struct value{
			T t_;
			template<class... A>
			value(A&&... a) :t_{ std::forward<A>(a)... } {}

		};
		template< class T >
		typename std::add_lvalue_reference<T>::type ldeclval();


	}

	template<class... T>
	struct values : T... {

	
	};

	template<class...V,class... T>
	struct values<values<V...>,T...> : V...,T... {

	
	
	};





	struct async_result {
	
	};

	
	struct dummy_coroutine_context {

		template<class... T>
		void operator()(T&&... t) {
		}

		operation do_return() { return operation::_return; };
		operation do_break() { return operation::_break; }
		operation do_continue() { return operation::_continue; };
		template<class... T>
		async_result do_async(){ return async_result{}; }

		dummy_coroutine_context() {}
		template<class T>
		dummy_coroutine_context(T&) {}



		

	};



	template<class CI>
	struct base_coroutine_context {
		CI* ci_;

		operation do_return() { return operation::_return; };
		base_coroutine_context(CI& ci) :ci_(&ci) {}
		};
	template<class Base>
	struct loop_coroutine_context:Base {
		using Base::Base;
		operation do_break() { return operation::_break; }
		operation do_continue() { return operation::_continue };
	
	};
	//template<class CI,class CP>
	//struct async_coroutine_context:base_coroutine_context<CI> {
	//	using base_coroutine_context<CI>::base_coroutine_context;
	//	template<class... T>
	//	void operator()(T&&... t) {
	//		CP::process(*this->ci_, this->ci_->value_, std::forward<T>(t)...);
	//	}
	//	template<class... T>
	//	async_result do_async(){ return async_result{}; }
	//


	//};

	template<class CI, class ReturnValue, std::size_t Pos, std::size_t Size,bool Loop>
	struct coroutine_processor;

	


	template<class CI,class CP,bool Async>
	struct async_coroutine_context_helper {
		using type = base_coroutine_context<CI>;

	};

	template<class CI,class CP,bool Loop, bool Async>
	struct coroutine_context_helper {
		using type = typename async_coroutine_context_helper<CI, CP, Async>::type;

	};

	template<class CI,class CP,bool Async>
	struct coroutine_context_helper<CI,CP,true,Async> {
		using base = typename async_coroutine_context_helper<CI, CP, Async>::type;
		using type = loop_coroutine_context<base>;

	};

	template<class CI, class CP,bool Loop, bool Async>
	using coroutine_context = typename coroutine_context_helper<CI, CP, Loop, Async>::type;


	template<class T>
	void assign_helper(T& v, T t) {
		v = std::move(t);
	}


	template<class CI, std::size_t Pos, std::size_t Size,bool Loop>
	struct coroutine_processor<CI,void,Pos,Size,Loop> {
		using value_type = typename CI::value_type;

		using self = coroutine_processor;
	
		template<class Finished>
		static operation do_next(CI& ci, value_type& value,Finished& , std::integral_constant<std::size_t,Size>) {
			return operation::_done;
		}
		template<class Finished,std::size_t P>
		static operation do_next(CI& ci, value_type& value,Finished& f, std::integral_constant<std::size_t,P>) {

			dummy_coroutine_context dc;
			using next_return = decltype(std::get<P>(ci.tuple_)(dc, ci.value_));

			return coroutine_processor<CI, next_return, P, Size,Loop>::process(ci, value,f);
		}
	
		template<class Finished,class... T>
		static operation process(CI& ci, value_type& value,Finished& f, T&&... results) {

			coroutine_context<CI, self, Loop, false> ctx{ ci };
			std::get<Pos>(ci.tuple_)(ctx, value,std::forward<T>(results)...);
			return do_next(ci, value,f, std::integral_constant<std::size_t,Pos + 1>{});
		}

	};
	template<class CI, std::size_t Pos, std::size_t Size,bool Loop>
	struct coroutine_processor<CI,operation,Pos,Size,Loop> {
		using value_type = typename CI::value_type;

	
		using self = coroutine_processor;
	
		template<class Finished>
		static operation do_next(CI& ci, value_type& value,Finished& f, std::integral_constant<std::size_t,Size>) {
			return operation::_done;
		}
		template<class Finished,std::size_t P>
		static operation do_next(CI& ci, value_type& value,Finished& f, std::integral_constant<std::size_t,P>) {
			dummy_coroutine_context dc;
			using next_return = decltype(std::get<P>(ci.tuple_)(dc, ci.value_));


			return coroutine_processor<CI, next_return, Pos + 1, Size,Loop>::process(ci, value,f);
		}
	
		template<class Finished,class... T>
		static operation process(CI& ci, value_type& value,Finished& f, T&&... results) {

			coroutine_context<CI, self, Loop, false> ctx{ ci };
			operation op = std::get<Pos>(ci.tuple_)(ctx, value,std::forward<T>(results)... );
			if (op == operation::_break) {
				return operation::_next;
			}
			else if (op == operation::_continue) {
				return operation::_continue;
			}
			else if (op == operation::_next) {
				return do_next(ci, value,f, std::integral_constant<std::size_t,Pos + 1>{});
			}
			else if (op == operation::_return) {
				return operation::_return;
			}
			else if (op == operation::_suspend) {
				return operation::_suspend;
			}
			throw std::logic_error{ "Invalid stackless_coroutine::operation" };
		}

	};

	template<class CI, std::size_t Pos, std::size_t Size,bool Loop>
	struct coroutine_processor<CI,async_result,Pos,Size,Loop> {

		using self = coroutine_processor;

	

		template<class Finished,class... T>
		static operation process(CI& ci, typename CI::value_type& value,Finished& f, T&&... results) {
				auto ctx = [ci = &ci,f = std::move(f)](auto&&... a)mutable {
					dummy_coroutine_context dc;
					using next_return = decltype(std::get<Pos + 1>(ci->tuple_)(dc, ci->value_, std::forward<decltype(a)>(a)...));

					using CP =  coroutine_processor<CI, next_return, Pos + 1, Size,Loop>;
					coroutine_context<CI, CP, Loop, true> ctx{ *ci };

					
					operation op;
					try {
						op = CP::process(*ci, ci->value_,f, std::forward<decltype(a)>(a)...);
					}
					catch (...) {
						auto ep = std::current_exception();
						f(ci->value_, ep,true,operation::_exception);
						return operation::_done;
					};

					if (op == operation::_done || op == operation::_return) {
						f(ci->value_, nullptr,true,op);
					}
					return op;
				};


	
			std::get<Pos>(ci.tuple_)(ctx, value,std::forward<T>(results)...);
			return operation::_suspend;
		}

	};


	template<class ValueType,bool Loop,class... T>
	struct coroutine{
		using value_type = ValueType;
		value_type value_;


		std::tuple<T...> tuple_;

		using lambda_tuple_t = std::tuple<T...>;

		enum { size = sizeof...(T) };
		enum { last_tuple_position= size - 1 };

		using self_t = coroutine;

		template<class V, class... A>
		coroutine(V v, A&&... a) :value_{ std::move(v) }, tuple_ { std::forward<A>(a)... } {}




		template<class Finished,class... A>
		auto run(Finished f,A&&... a) {

			dummy_coroutine_context dc;
			using ret_type = decltype(std::get<0>(tuple_)(dc, value_, std::forward<A>(a)...));
			
			operation op;
			try {
				op = coroutine_processor<self_t, ret_type, 0, size, false>::process(*this, value_,f, std::forward<A>(a)...);
			}
			catch (...) {
				auto ep = std::current_exception();
				f(value_, ep,false,operation::_exception);

			};

			if (op == operation::_done || op == operation::_done) {
				f(value_, nullptr,false,op);
			}
			return op;
	}

		value_type& value() { return value_; }
		


		







	};



	template<class ValueFunc,class... T>
	auto make_coroutine(ValueFunc&& vf,T&&... t) {

			return coroutine<std::decay_t<decltype(vf())>, false, std::decay_t<T>...> { vf(),  std::forward<T>(t)... };

		
	}




};


int main() {


	auto ci = stackless_coroutine::make_coroutine(
		
		[]() {
		struct val {
			int x;
			int y;
			int z;
		};


		return val{ 0,1,2 };


		},
		[](auto& a, auto& b) {
			return stackless_coroutine::operation::_return;
		},
		[](auto& a, auto& b) {
			a(1);
			return stackless_coroutine::async_result();

		},
		[](auto& a, auto& b,int v) {
			a(1, 2);
			return stackless_coroutine::async_result();

		},
		[](auto& a, auto& b, int x, int y) {
			b.y = b.x + b.y + b.z;
			return stackless_coroutine::operation::_next;

		}


		)
;


	ci.run(		[](auto& a, std::exception_ptr, bool as,auto op) {
			std::cout << "Finished\n";
	});
	

	auto& v = ci.value();

	
	std::cout << v.y << std::endl;
};