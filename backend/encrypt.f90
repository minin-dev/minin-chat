! ============================================================
! MININ-CHAT ENCRYPTION ENGINE v1.0
! Fortran 90 Stream Cipher Module
! ------------------------------------------------------------
! Vigenere-PRNG hybrid cipher operating in printable ASCII
! range [32-126]. Each character is shifted by a pseudorandom
! keystream byte derived from a linear congruential generator.
!
! Called from C via iso_c_binding interface.
! ============================================================
module minin_crypto
  use iso_c_binding
  implicit none
  private
  public :: minin_encrypt, minin_decrypt, minin_hash

contains

  ! ----------------------------------------------------------
  ! ENCRYPT: Printable-ASCII stream cipher
  !   input(msglen)  -> plaintext  (printable ASCII)
  !   output(msglen) -> ciphertext (printable ASCII)
  !   key            -> integer seed for PRNG
  ! ----------------------------------------------------------
  subroutine minin_encrypt(input, output, msglen, key) &
      bind(C, name="minin_encrypt")
    integer(c_int), intent(in) :: msglen, key
    character(kind=c_char), intent(in)  :: input(*)
    character(kind=c_char), intent(out) :: output(*)

    integer :: i, ch, shift, state

    state = key
    do i = 1, msglen
      ! Linear congruential PRNG (Numerical Recipes constants)
      state = ieor(state * 1103515245 + 12345, ishft(state, -16))
      shift = mod(iand(abs(state), 65535), 95)

      ch = ichar(input(i))
      if (ch >= 32 .and. ch <= 126) then
        ch = mod(ch - 32 + shift, 95) + 32
      end if
      output(i) = achar(ch)
    end do
  end subroutine minin_encrypt

  ! ----------------------------------------------------------
  ! DECRYPT: Reverse the stream cipher
  ! ----------------------------------------------------------
  subroutine minin_decrypt(input, output, msglen, key) &
      bind(C, name="minin_decrypt")
    integer(c_int), intent(in) :: msglen, key
    character(kind=c_char), intent(in)  :: input(*)
    character(kind=c_char), intent(out) :: output(*)

    integer :: i, ch, shift, state

    state = key
    do i = 1, msglen
      state = ieor(state * 1103515245 + 12345, ishft(state, -16))
      shift = mod(iand(abs(state), 65535), 95)

      ch = ichar(input(i))
      if (ch >= 32 .and. ch <= 126) then
        ch = mod(ch - 32 - shift + 9500, 95) + 32
      end if
      output(i) = achar(ch)
    end do
  end subroutine minin_decrypt

  ! ----------------------------------------------------------
  ! HASH: Simple hash for tokens (16-char alphanumeric output)
  ! ----------------------------------------------------------
  subroutine minin_hash(input, output, msglen) &
      bind(C, name="minin_hash")
    integer(c_int), intent(in) :: msglen
    character(kind=c_char), intent(in)  :: input(*)
    character(kind=c_char), intent(out) :: output(*)

    integer :: i, h1, h2, h3, h4
    integer :: byte_val

    h1 = 5381
    h2 = 63689
    h3 = 0
    h4 = 1

    do i = 1, msglen
      byte_val = ichar(input(i))
      h1 = ieor(h1 * 33, byte_val)
      h2 = ieor(h2 * byte_val, ishft(h2, -3))
      h3 = h3 + byte_val * i * 7
      h4 = ieor(h4 * 31 + byte_val, h1)
    end do

    ! Convert hash state to 16 hex-like chars [a-p]
    do i = 1, 4
      output(i)    = achar(mod(iand(abs(h1), 255), 16) + 97)
      output(i+4)  = achar(mod(iand(abs(h2), 255), 16) + 97)
      output(i+8)  = achar(mod(iand(abs(h3), 255), 16) + 97)
      output(i+12) = achar(mod(iand(abs(h4), 255), 16) + 97)
      h1 = ishft(h1, -8)
      h2 = ishft(h2, -8)
      h3 = ishft(h3, -8)
      h4 = ishft(h4, -8)
    end do
  end subroutine minin_hash

end module minin_crypto
