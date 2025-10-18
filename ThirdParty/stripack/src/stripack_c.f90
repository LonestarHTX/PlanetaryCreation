module stripack_c
  use iso_c_binding
  implicit none
  contains
  subroutine stripack_triangulate(n, xyz, ntri, tri) bind(C, name="stripack_triangulate")
  ! Inputs
  integer(c_int), value :: n
  real(c_double), intent(in) :: xyz(3, n)   ! column-major: xyz(:,i) = [x;y;z] for point i

      ! Outputs
      integer(c_int), intent(out) :: ntri
      integer(c_int), intent(out) :: tri(3, *)  ! caller allocates >= 3*(2*n) ints

      ! Local / work
      integer :: ier, lnew, nt, i
      integer, allocatable :: list(:), lptr(:), lend(:), near(:), next(:)
      real(c_double), allocatable :: dist(:)
      integer, allocatable :: ltri(:,:)         ! (6, nt)

      ! Allocate STRIPACK work arrays (see stripack.f90 docs)
      allocate(list(6*(n-2)), lptr(6*(n-2)), lend(n), near(n), next(n), dist(n))

      ! Call TRMESH to build triangulation
      call trmesh(n, xyz(1,1), xyz(2,1), xyz(3,1), list, lptr, lend, lnew, near, next, dist, ier)
      if (ier /= 0) then
        ntri = 0
        return
      end if

      ! Extract triangles with TRLIST (nrow = 6)
      allocate(ltri(6, 2*n))
      call trlist(n, list, lptr, lend, 6, nt, ltri, ier)
      if (ier /= 0) then
        ntri = 0
        return
      end if

      ! Convert to 0-based vertex indices for C++
      ntri = nt
      do i = 1, nt
        tri(1,i) = ltri(1,i) - 1
        tri(2,i) = ltri(2,i) - 1
        tri(3,i) = ltri(3,i) - 1
      end do

      if (allocated(list)) deallocate(list)
      if (allocated(lptr)) deallocate(lptr)
      if (allocated(lend)) deallocate(lend)
      if (allocated(near)) deallocate(near)
      if (allocated(next)) deallocate(next)
      if (allocated(dist)) deallocate(dist)
      if (allocated(ltri)) deallocate(ltri)

  end subroutine
  end module