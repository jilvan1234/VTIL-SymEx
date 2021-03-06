// Copyright (c) 2020 Can Boluk and contributors of the VTIL Project   
// All rights reserved.   
//    
// Redistribution and use in source and binary forms, with or without   
// modification, are permitted provided that the following conditions are met: 
//    
// 1. Redistributions of source code must retain the above copyright notice,   
//    this list of conditions and the following disclaimer.   
// 2. Redistributions in binary form must reproduce the above copyright   
//    notice, this list of conditions and the following disclaimer in the   
//    documentation and/or other materials provided with the distribution.   
// 3. Neither the name of mosquitto nor the names of its   
//    contributors may be used to endorse or promote products derived from   
//    this software without specific prior written permission.   
//    
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE   
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE  
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE   
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR   
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF   
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS   
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN   
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)   
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE  
// POSSIBILITY OF SUCH DAMAGE.        
//
#include "expression.hpp"
#include <vtil/io>
#include "..\simplifier\simplifier.hpp"

namespace vtil::symbolic
{
	// FNV hash is used for hashing of the expressions.
	//
	static constexpr size_t fnv_initial = 0xcbf29ce484222325;

	template<typename T>
	static inline void fnv_append( size_t* hash, const T& ref )
	{
		for ( size_t i = 0; i < sizeof( T ); i++ )
			*hash = ( ( ( const uint8_t* ) &ref )[ i ] ^ *hash ) * 0x100000001B3;
	}

	// Returns the number of constants used in the expression.
	//
	size_t expression::count_constants() const
	{
		if ( is_constant() )
			return 1;
		return ( lhs ? lhs->count_constants() : 0 ) +
			( rhs ? rhs->count_constants() : 0 );
	}

	// Returns the number of variables used in the expression.
	//
	size_t expression::count_variables() const
	{
		if ( is_variable() )
			return 1;
		return ( lhs ? lhs->count_variables() : 0 ) +
			( rhs ? rhs->count_variables() : 0 );
	}

	// Returns the number of unique variables used in the expression.
	//
	size_t expression::count_unique_variables( std::set<unique_identifier>* visited ) const
	{
		std::set<unique_identifier> tmp;
		if ( !visited ) visited = &tmp;

		if ( is_variable() )
			return visited->find( uid ) == visited->end();

		return ( lhs ? lhs->count_unique_variables( visited ) : 0 ) +
			   ( rhs ? rhs->count_unique_variables( visited ) : 0 );
	}

	// Resizes the expression, if not constant, expression::resize will try to propagate 
	// the operation as deep as possible.
	//
	void expression::resize( uint8_t new_size, bool signed_cast )
	{
		// If requested size is equal, skip.
		//
		if ( value.size() == new_size ) return;

		// If boolean requested, signed cast is not valid.
		//
		if ( new_size == 1 ) signed_cast = false;

		switch ( op )
		{
			// If constant resize the value, if variable apply the operation as is.
			// 
			case math::operator_id::invalid:
				if ( is_constant() )
				{
					value = value.resize( new_size, signed_cast );
					update( false );
				}
				else
				{
					if ( signed_cast )
						*this = __cast( *this, new_size ).simplify();
					else
						*this = __ucast( *this, new_size ).simplify();
				}
				break;

			// If basic unsigned operation, unsigned cast both sides if requested type is also unsigned.
			//
			case math::operator_id::bitwise_and:
			case math::operator_id::bitwise_or:
			case math::operator_id::bitwise_xor:
			case math::operator_id::bitwise_not:
			case math::operator_id::umultiply:
			case math::operator_id::udivide:
			case math::operator_id::uremainder:
			case math::operator_id::umax_value:
			case math::operator_id::umin_value:
				if ( !signed_cast )
				{
					if ( lhs && lhs->size() != new_size ) ( +lhs )->resize( new_size, false );
					if ( rhs->size() != new_size ) ( +rhs )->resize( new_size, false );
					update( true );
				}
				else
				{
					*this = __cast( *this, new_size ).simplify();
				}
				break;
				
			// If basic signed operation, signed cast both sides if requested type is also signed.
			//
			case math::operator_id::multiply:
			case math::operator_id::divide:
			case math::operator_id::remainder:
			case math::operator_id::add:
			case math::operator_id::negate:
			case math::operator_id::substract:
			case math::operator_id::max_value:
			case math::operator_id::min_value:
				if ( signed_cast )
				{
					if ( lhs && lhs->size() != new_size ) ( +lhs )->resize( new_size, true );
					if ( rhs->size() != new_size ) ( +rhs )->resize( new_size, true );
					update( true );
				}
				else
				{
					*this = __ucast( *this, new_size ).simplify();
				}
				break;

			// If casting the result of an unsigned cast, just change the parameter.
			//
			case math::operator_id::ucast:
				// If it was shrinked:
				//
				if ( lhs->size() > rhs->get().value() )
				{
					*this = __ucast( ( *this & expression{ math::fill( rhs->get().value() ), lhs->size() } ).simplify(), new_size ).simplify();
					break;
				}
				// If sizes match, escape cast operator.
				//
				else if ( lhs->size() == new_size )
				{
					*this = *lhs;
				}
				// Otherwise upgrade the parameter.
				//
				else
				{
					rhs = new_size;
					update( true );
				}
				break;

			// If casting the result of a signed cast, change the parameter if 
			// requested cast is also a signed.
			//
			case math::operator_id::cast:
				// If it was shrinked:
				//
				if ( lhs->size() > rhs->get().value() )
				{
					*this = __cast( ( *this & expression{ math::fill( rhs->get().value() ), lhs->size() } ).simplify(), new_size ).simplify();
					break;
				}
				// If sizes match, escape cast operator.
				//
				else if ( lhs->size() == new_size )
				{
					*this = *lhs;
				}
				// Otherwise, if both are signed upgrade the parameter.
				//
				else if ( signed_cast )
				{
					rhs = new_size;
					update( true );
				}
				// Else, convert to unsigned cast since top bits will be zero.
				//
				else
				{
					*this = __ucast( *this, new_size ).simplify();
				}
				break;

			// Redirect to conditional output since zx 0 == sx 0.
			//
			case math::operator_id::value_if:
				( +rhs )->resize( new_size, false );
				update( true );
				break;

			// Boolean operators will ignore resizing requests.
			//
			case math::operator_id::bit_test:
			case math::operator_id::greater:
			case math::operator_id::greater_eq:
			case math::operator_id::equal:
			case math::operator_id::not_equal:
			case math::operator_id::less_eq:
			case math::operator_id::less:
			case math::operator_id::ugreater:
			case math::operator_id::ugreater_eq:
			case math::operator_id::uless_eq:
			case math::operator_id::uless:
				hash += new_size - value.size();
				value.resize( new_size, false );
				break;

			// If no handler found:
			//
			default:
				if ( signed_cast )
					*this = __cast( *this, new_size ).simplify();
				else
					*this = __ucast( *this, new_size ).simplify();
				break;
		}
	}


	// Updates the expression state.
	//
	void expression::update( bool auto_simplify )
	{
		// If it's not a full expression tree:
		//
		if ( !is_expression() )
		{
			// Reset depth.
			//
			depth = 0;

			// If constant value:
			//
			if ( is_constant() )
			{
				// Punish for each set bit in [min_{popcnt x}(v, |v|)], in an exponentially decreasing rate.
				//
				int64_t cval = *value.get<true>();
				complexity = sqrt( 1 + std::min( math::popcnt( cval ), math::popcnt( abs( cval ) ) ) );

				// Hash begins as initial FNV value incremented by the constant, after which we append the notted constant and its size.
				//
				hash = fnv_initial + value.known_one();
				fnv_append( &hash, value.size() );
				fnv_append( &hash, value.known_zero() );
			}
			// If symbolic variable
			//
			else
			{
				fassert( is_variable() );

				// Assign the constant complexity value.
				//
				complexity = 128;

				// Hash is inherited with the addition of the size.
				//
				hash = uid.hash + value.size();
			}

			// Set simplification state.
			//
			simplify_hint = true;
		}
		else
		{
			fassert( is_expression() );

			// If unary operator:
			//
			const math::operator_desc* desc = get_op_desc();
			if ( desc->operand_count == 1 )
			{
				// Partially evaluate the expression.
				//
				value = math::evaluate_partial( op, {}, rhs->value );

				// Calculate base complexity and the depth.
				//
				depth = rhs->depth + 1;
				complexity = rhs->complexity * 2;
				fassert( complexity != 0 );
				
				// Inherit the RHS hash and...
				//
				hash = fnv_initial;
				fnv_append( &hash, rhs->hash );
			}
			// If binary operator:
			//
			else
			{
				fassert( desc->operand_count == 2 );

				// If operation is __cast or __ucast, right hand side must always be a constant, propagate 
				// left hand side value and resize as requested.
				//
				if ( op == math::operator_id::ucast || op == math::operator_id::cast )
				{
					value = lhs->value;
					value.resize( rhs->get<uint8_t>().value(), op == math::operator_id::cast );
				}
				// Partially evaluate the expression if not resize.
				//
				else
				{
					value = math::evaluate_partial( op, lhs->value, rhs->value );
				}

				// Handle size mismatches.
				//
				switch ( op )
				{
					case math::operator_id::bitwise_and:
					case math::operator_id::bitwise_or:
					case math::operator_id::bitwise_xor:
					case math::operator_id::umultiply_high:
					case math::operator_id::umultiply:
					case math::operator_id::udivide:
					case math::operator_id::uremainder:
					case math::operator_id::umax_value:
					case math::operator_id::umin_value:
						if ( lhs->size() != value.size() ) ( +lhs )->resize( value.size(), false );
						if ( rhs->size() != value.size() ) ( +rhs )->resize( value.size(), false );
						break;
					case math::operator_id::multiply_high:
					case math::operator_id::multiply:
					case math::operator_id::divide:
					case math::operator_id::remainder:
					case math::operator_id::add:
					case math::operator_id::substract:
					case math::operator_id::max_value:
					case math::operator_id::min_value:
						if ( lhs->size() != value.size() ) ( +lhs )->resize( value.size(), true );
						if ( rhs->size() != value.size() ) ( +rhs )->resize( value.size(), true );
					default:
						break;
				}

				// Calculate base complexity and the depth.
				//
				depth = std::max( lhs->depth, rhs->depth ) + 1;
				complexity = ( lhs->complexity + rhs->complexity ) * 2;
				fassert( complexity != 0 );

				// If operator is commutative, calculate hash in a way that 
				// the positions of operands do not matter.
				//
				if ( desc->is_commutative )
				{
					hash = fnv_initial;
					fnv_append( &hash, std::max( lhs->hash, rhs->hash ) );
					fnv_append( &hash, rhs->hash ^ lhs->hash );
				}
				// Else inherit the RHS hash and append LHS hash.
				//
				else
				{
					hash = fnv_initial;
					fnv_append( &hash, lhs->hash );
					fnv_append( &hash, rhs->hash );
				}
			}

			// Append depth and operator information to the hash.
			//
			fnv_append( &hash, op );
			fnv_append( &hash, depth );

			// Punish for mixing bitwise and arithmetic operators.
			//
			for ( auto& operand : { &lhs, &rhs } )
			{
				if ( *operand && operand->reference->is_expression() )
				{
					// Bitwise hint of the descriptor contains +1 or -1 if the operator
					// is strictly bitwise or arithmetic respectively and 0 otherwise.
					// This works since mulitplication between them will only be negative
					// if the hints mismatch.
					//
					complexity *= 1 + math::sgn( operand->reference->get_op_desc()->hint_bitwise * desc->hint_bitwise );
				}
			}
			
			// Reset simplification state since expression was updated.
			//
			simplify_hint = false;
		
			// If auto simplification is enabled, invoke it.
			//
			if ( auto_simplify ) simplify();
		}
	}

	// Simplifies the expression.
	//
	expression& expression::simplify( bool prettify )
	{
		// By changing the prototype of simplify_expression from f(expression&) to
		// f(expression::reference&), we gain an important performance benefit that is
		// a significantly less amount of copies made. Cache will also store references 
		// this way and additionally we avoid copying where an operand is being simplified
		// as that can be replaced by a simple swap of shared references.
		//
		auto ref = make_local_reference( this );
		simplify_expression( ref, prettify );

		// Only thing that we should be careful about is the case expression->simplify(),
		// which is problematic since it is not actually a shared reference, which we can
		// carefully solve with the local reference system (which will assert that, no 
		// references to it was stored on destructor for us) and making sure to copy data
		// if the pointer was replaced.
		//
		if ( &*ref != this ) 
			operator=( *ref );
		else
			fassert( ref.reference.use_count() == 1 );

		// Set the simplifier hint to indicate skipping further calls to simplify_expression.
		//
		simplify_hint = true;
		return *this;
	}

	// Returns whether the given expression is equivalent to the current instance.
	//
	bool expression::equals( const expression& other ) const
	{
		// If hash mismatch, return false without checking anything.
		//
		if ( hash != other.hash )
			return false;

		// If operator or the sizes are not the same, return false.
		//
		if ( op != other.op || size() != other.size() )
			return false;

		// If variable, check if the identifiers match.
		//
		if ( is_variable() )
			return other.is_variable() && uid == other.uid;

		// If constant, check if the constants match.
		//
		if ( is_constant() )
			return other.is_constant() && value == other.value;

		// Resolve operator descriptor, if unary, just compare right hand side.
		//
		const math::operator_desc* desc = get_op_desc();
		if ( desc->operand_count == 1 )
			return rhs == other.rhs || rhs->equals( *other.rhs );

		// If both sides match, return true.
		//
		if ( ( lhs == other.lhs || lhs->equals( *other.lhs ) ) &&
			 ( rhs == other.rhs || rhs->equals( *other.rhs ) ))
			return true;

		// If not, check in reverse as well if commutative and return the final result.
		//
		return	desc->is_commutative && 
				( lhs == other.rhs || lhs->equals( *other.rhs ) ) &&
				( rhs == other.lhs || rhs->equals( *other.lhs ) );
	}

	// Converts to human-readable format.
	//
	std::string expression::to_string() const
	{
		// Redirect to operator descriptor.
		//
		if ( is_expression() )
			return get_op_desc()->to_string( lhs ? lhs->to_string() : "", rhs->to_string() );

		// Handle constants, invalids and variables.
		// -- TODO: Fix variable case, small hack for now
		//
		if ( is_constant() )      return format::hex( value.get<true>().value() );
		if ( is_variable() )      return format::str( "%s:%d", ( const char* ) uid.ptr, size() );
		return "NULL";
	}
};
